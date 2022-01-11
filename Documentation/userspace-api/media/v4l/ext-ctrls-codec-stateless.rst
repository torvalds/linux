.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _codec-stateless-controls:

*********************************
Stateless Codec Control Reference
*********************************

The Stateless Codec control class is intended to support
stateless decoder and encoders (i.e. hardware accelerators).

These drivers are typically supported by the :ref:`stateless_decoder`,
and deal with parsed pixel formats such as V4L2_PIX_FMT_H264_SLICE.

Stateless Codec Control ID
==========================

.. _codec-stateless-control-id:

``V4L2_CID_CODEC_STATELESS_CLASS (class)``
    The Stateless Codec class descriptor.

.. _v4l2-codec-stateless-h264:

``V4L2_CID_STATELESS_H264_SPS (struct)``
    Specifies the sequence parameter set (as extracted from the
    bitstream) for the associated H264 slice data. This includes the
    necessary parameters for configuring a stateless hardware decoding
    pipeline for H264. The bitstream parameters are defined according
    to :ref:`h264`, section 7.4.2.1.1 "Sequence Parameter Set Data
    Semantics". For further documentation, refer to the above
    specification, unless there is an explicit comment stating
    otherwise.

.. c:type:: v4l2_ctrl_h264_sps

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.2cm}|p{8.6cm}|p{7.5cm}|

.. flat-table:: struct v4l2_ctrl_h264_sps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``profile_idc``
      -
    * - __u8
      - ``constraint_set_flags``
      - See :ref:`Sequence Parameter Set Constraints Set Flags <h264_sps_constraints_set_flags>`
    * - __u8
      - ``level_idc``
      -
    * - __u8
      - ``seq_parameter_set_id``
      -
    * - __u8
      - ``chroma_format_idc``
      -
    * - __u8
      - ``bit_depth_luma_minus8``
      -
    * - __u8
      - ``bit_depth_chroma_minus8``
      -
    * - __u8
      - ``log2_max_frame_num_minus4``
      -
    * - __u8
      - ``pic_order_cnt_type``
      -
    * - __u8
      - ``log2_max_pic_order_cnt_lsb_minus4``
      -
    * - __u8
      - ``max_num_ref_frames``
      -
    * - __u8
      - ``num_ref_frames_in_pic_order_cnt_cycle``
      -
    * - __s32
      - ``offset_for_ref_frame[255]``
      -
    * - __s32
      - ``offset_for_non_ref_pic``
      -
    * - __s32
      - ``offset_for_top_to_bottom_field``
      -
    * - __u16
      - ``pic_width_in_mbs_minus1``
      -
    * - __u16
      - ``pic_height_in_map_units_minus1``
      -
    * - __u32
      - ``flags``
      - See :ref:`Sequence Parameter Set Flags <h264_sps_flags>`

.. raw:: latex

    \normalsize

.. _h264_sps_constraints_set_flags:

``Sequence Parameter Set Constraints Set Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_SPS_CONSTRAINT_SET0_FLAG``
      - 0x00000001
      -
    * - ``V4L2_H264_SPS_CONSTRAINT_SET1_FLAG``
      - 0x00000002
      -
    * - ``V4L2_H264_SPS_CONSTRAINT_SET2_FLAG``
      - 0x00000004
      -
    * - ``V4L2_H264_SPS_CONSTRAINT_SET3_FLAG``
      - 0x00000008
      -
    * - ``V4L2_H264_SPS_CONSTRAINT_SET4_FLAG``
      - 0x00000010
      -
    * - ``V4L2_H264_SPS_CONSTRAINT_SET5_FLAG``
      - 0x00000020
      -

.. _h264_sps_flags:

``Sequence Parameter Set Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE``
      - 0x00000001
      -
    * - ``V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS``
      - 0x00000002
      -
    * - ``V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO``
      - 0x00000004
      -
    * - ``V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED``
      - 0x00000008
      -
    * - ``V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY``
      - 0x00000010
      -
    * - ``V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD``
      - 0x00000020
      -
    * - ``V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE``
      - 0x00000040
      -

``V4L2_CID_STATELESS_H264_PPS (struct)``
    Specifies the picture parameter set (as extracted from the
    bitstream) for the associated H264 slice data. This includes the
    necessary parameters for configuring a stateless hardware decoding
    pipeline for H264.  The bitstream parameters are defined according
    to :ref:`h264`, section 7.4.2.2 "Picture Parameter Set RBSP
    Semantics". For further documentation, refer to the above
    specification, unless there is an explicit comment stating
    otherwise.

.. c:type:: v4l2_ctrl_h264_pps

.. raw:: latex

    \small

.. flat-table:: struct v4l2_ctrl_h264_pps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``pic_parameter_set_id``
      -
    * - __u8
      - ``seq_parameter_set_id``
      -
    * - __u8
      - ``num_slice_groups_minus1``
      -
    * - __u8
      - ``num_ref_idx_l0_default_active_minus1``
      -
    * - __u8
      - ``num_ref_idx_l1_default_active_minus1``
      -
    * - __u8
      - ``weighted_bipred_idc``
      -
    * - __s8
      - ``pic_init_qp_minus26``
      -
    * - __s8
      - ``pic_init_qs_minus26``
      -
    * - __s8
      - ``chroma_qp_index_offset``
      -
    * - __s8
      - ``second_chroma_qp_index_offset``
      -
    * - __u16
      - ``flags``
      - See :ref:`Picture Parameter Set Flags <h264_pps_flags>`

.. raw:: latex

    \normalsize

.. _h264_pps_flags:

``Picture Parameter Set Flags``

.. raw:: latex

    \begingroup
    \scriptsize
    \setlength{\tabcolsep}{2pt}

.. tabularcolumns:: |p{9.8cm}|p{1.0cm}|p{6.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       10 1 4

    * - ``V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE``
      - 0x0001
      -
    * - ``V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT``
      - 0x0002
      -
    * - ``V4L2_H264_PPS_FLAG_WEIGHTED_PRED``
      - 0x0004
      -
    * - ``V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT``
      - 0x0008
      -
    * - ``V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED``
      - 0x0010
      -
    * - ``V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT``
      - 0x0020
      -
    * - ``V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE``
      - 0x0040
      -
    * - ``V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT``
      - 0x0080
      - ``V4L2_CID_STATELESS_H264_SCALING_MATRIX``
        must be used for this picture.

.. raw:: latex

    \endgroup

``V4L2_CID_STATELESS_H264_SCALING_MATRIX (struct)``
    Specifies the scaling matrix (as extracted from the bitstream) for
    the associated H264 slice data. The bitstream parameters are
    defined according to :ref:`h264`, section 7.4.2.1.1.1 "Scaling
    List Semantics". For further documentation, refer to the above
    specification, unless there is an explicit comment stating
    otherwise.

.. c:type:: v4l2_ctrl_h264_scaling_matrix

.. raw:: latex

    \small

.. tabularcolumns:: |p{0.6cm}|p{4.8cm}|p{11.9cm}|

.. flat-table:: struct v4l2_ctrl_h264_scaling_matrix
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``scaling_list_4x4[6][16]``
      - Scaling matrix after applying the inverse scanning process.
        Expected list order is Intra Y, Intra Cb, Intra Cr, Inter Y,
        Inter Cb, Inter Cr. The values on each scaling list are
        expected in raster scan order.
    * - __u8
      - ``scaling_list_8x8[6][64]``
      - Scaling matrix after applying the inverse scanning process.
        Expected list order is Intra Y, Inter Y, Intra Cb, Inter Cb,
        Intra Cr, Inter Cr. The values on each scaling list are
        expected in raster scan order.

``V4L2_CID_STATELESS_H264_SLICE_PARAMS (struct)``
    Specifies the slice parameters (as extracted from the bitstream)
    for the associated H264 slice data. This includes the necessary
    parameters for configuring a stateless hardware decoding pipeline
    for H264.  The bitstream parameters are defined according to
    :ref:`h264`, section 7.4.3 "Slice Header Semantics". For further
    documentation, refer to the above specification, unless there is
    an explicit comment stating otherwise.

.. c:type:: v4l2_ctrl_h264_slice_params

.. raw:: latex

    \small

.. tabularcolumns:: |p{4.0cm}|p{5.9cm}|p{7.4cm}|

.. flat-table:: struct v4l2_ctrl_h264_slice_params
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``header_bit_size``
      - Offset in bits to slice_data() from the beginning of this slice.
    * - __u32
      - ``first_mb_in_slice``
      -
    * - __u8
      - ``slice_type``
      -
    * - __u8
      - ``colour_plane_id``
      -
    * - __u8
      - ``redundant_pic_cnt``
      -
    * - __u8
      - ``cabac_init_idc``
      -
    * - __s8
      - ``slice_qp_delta``
      -
    * - __s8
      - ``slice_qs_delta``
      -
    * - __u8
      - ``disable_deblocking_filter_idc``
      -
    * - __s8
      - ``slice_alpha_c0_offset_div2``
      -
    * - __s8
      - ``slice_beta_offset_div2``
      -
    * - __u8
      - ``num_ref_idx_l0_active_minus1``
      - If num_ref_idx_active_override_flag is not set, this field must be
        set to the value of num_ref_idx_l0_default_active_minus1
    * - __u8
      - ``num_ref_idx_l1_active_minus1``
      - If num_ref_idx_active_override_flag is not set, this field must be
        set to the value of num_ref_idx_l1_default_active_minus1
    * - __u8
      - ``reserved``
      - Applications and drivers must set this to zero.
    * - struct :c:type:`v4l2_h264_reference`
      - ``ref_pic_list0[32]``
      - Reference picture list after applying the per-slice modifications
    * - struct :c:type:`v4l2_h264_reference`
      - ``ref_pic_list1[32]``
      - Reference picture list after applying the per-slice modifications
    * - __u32
      - ``flags``
      - See :ref:`Slice Parameter Flags <h264_slice_flags>`

.. raw:: latex

    \normalsize

.. _h264_slice_flags:

``Slice Parameter Set Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED``
      - 0x00000001
      -
    * - ``V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH``
      - 0x00000002
      -

``V4L2_CID_STATELESS_H264_PRED_WEIGHTS (struct)``
    Prediction weight table defined according to :ref:`h264`,
    section 7.4.3.2 "Prediction Weight Table Semantics".
    The prediction weight table must be passed by applications
    under the conditions explained in section 7.3.3 "Slice header
    syntax".

.. c:type:: v4l2_ctrl_h264_pred_weights

.. raw:: latex

    \small

.. tabularcolumns:: |p{4.9cm}|p{4.9cm}|p{7.5cm}|

.. flat-table:: struct v4l2_ctrl_h264_pred_weights
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u16
      - ``luma_log2_weight_denom``
      -
    * - __u16
      - ``chroma_log2_weight_denom``
      -
    * - struct :c:type:`v4l2_h264_weight_factors`
      - ``weight_factors[2]``
      - The weight factors at index 0 are the weight factors for the reference
        list 0, the one at index 1 for the reference list 1.

.. raw:: latex

    \normalsize

.. c:type:: v4l2_h264_weight_factors

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.0cm}|p{4.5cm}|p{11.8cm}|

.. flat-table:: struct v4l2_h264_weight_factors
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s16
      - ``luma_weight[32]``
      -
    * - __s16
      - ``luma_offset[32]``
      -
    * - __s16
      - ``chroma_weight[32][2]``
      -
    * - __s16
      - ``chroma_offset[32][2]``
      -

.. raw:: latex

    \normalsize

``Picture Reference``

.. c:type:: v4l2_h264_reference

.. cssclass:: longtable

.. flat-table:: struct v4l2_h264_reference
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``fields``
      - Specifies how the picture is referenced. See :ref:`Reference Fields <h264_ref_fields>`
    * - __u8
      - ``index``
      - Index into the :c:type:`v4l2_ctrl_h264_decode_params`.dpb array.

.. _h264_ref_fields:

``Reference Fields``

.. raw:: latex

    \small

.. tabularcolumns:: |p{5.4cm}|p{0.8cm}|p{11.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_TOP_FIELD_REF``
      - 0x1
      - The top field in field pair is used for short-term reference.
    * - ``V4L2_H264_BOTTOM_FIELD_REF``
      - 0x2
      - The bottom field in field pair is used for short-term reference.
    * - ``V4L2_H264_FRAME_REF``
      - 0x3
      - The frame (or the top/bottom fields, if it's a field pair)
        is used for short-term reference.

.. raw:: latex

    \normalsize

``V4L2_CID_STATELESS_H264_DECODE_PARAMS (struct)``
    Specifies the decode parameters (as extracted from the bitstream)
    for the associated H264 slice data. This includes the necessary
    parameters for configuring a stateless hardware decoding pipeline
    for H264. The bitstream parameters are defined according to
    :ref:`h264`. For further documentation, refer to the above
    specification, unless there is an explicit comment stating
    otherwise.

.. c:type:: v4l2_ctrl_h264_decode_params

.. raw:: latex

    \small

.. tabularcolumns:: |p{4.0cm}|p{5.9cm}|p{7.4cm}|

.. flat-table:: struct v4l2_ctrl_h264_decode_params
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - struct :c:type:`v4l2_h264_dpb_entry`
      - ``dpb[16]``
      -
    * - __u16
      - ``nal_ref_idc``
      - NAL reference ID value coming from the NAL Unit header
    * - __u16
      - ``frame_num``
      -
    * - __s32
      - ``top_field_order_cnt``
      - Picture Order Count for the coded top field
    * - __s32
      - ``bottom_field_order_cnt``
      - Picture Order Count for the coded bottom field
    * - __u16
      - ``idr_pic_id``
      -
    * - __u16
      - ``pic_order_cnt_lsb``
      -
    * - __s32
      - ``delta_pic_order_cnt_bottom``
      -
    * - __s32
      - ``delta_pic_order_cnt0``
      -
    * - __s32
      - ``delta_pic_order_cnt1``
      -
    * - __u32
      - ``dec_ref_pic_marking_bit_size``
      - Size in bits of the dec_ref_pic_marking() syntax element.
    * - __u32
      - ``pic_order_cnt_bit_size``
      - Combined size in bits of the picture order count related syntax
        elements: pic_order_cnt_lsb, delta_pic_order_cnt_bottom,
        delta_pic_order_cnt0, and delta_pic_order_cnt1.
    * - __u32
      - ``slice_group_change_cycle``
      -
    * - __u32
      - ``reserved``
      - Applications and drivers must set this to zero.
    * - __u32
      - ``flags``
      - See :ref:`Decode Parameters Flags <h264_decode_params_flags>`

.. raw:: latex

    \normalsize

.. _h264_decode_params_flags:

``Decode Parameters Flags``

.. raw:: latex

    \small

.. tabularcolumns:: |p{8.3cm}|p{2.1cm}|p{6.9cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC``
      - 0x00000001
      - That picture is an IDR picture
    * - ``V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC``
      - 0x00000002
      -
    * - ``V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD``
      - 0x00000004
      -

.. raw:: latex

    \normalsize

.. c:type:: v4l2_h264_dpb_entry

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.0cm}|p{4.9cm}|p{11.4cm}|

.. flat-table:: struct v4l2_h264_dpb_entry
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u64
      - ``reference_ts``
      - Timestamp of the V4L2 capture buffer to use as reference, used
        with B-coded and P-coded frames. The timestamp refers to the
        ``timestamp`` field in struct :c:type:`v4l2_buffer`. Use the
        :c:func:`v4l2_timeval_to_ns()` function to convert the struct
        :c:type:`timeval` in struct :c:type:`v4l2_buffer` to a __u64.
    * - __u32
      - ``pic_num``
      -
    * - __u16
      - ``frame_num``
      -
    * - __u8
      - ``fields``
      - Specifies how the DPB entry is referenced. See :ref:`Reference Fields <h264_ref_fields>`
    * - __u8
      - ``reserved[5]``
      - Applications and drivers must set this to zero.
    * - __s32
      - ``top_field_order_cnt``
      -
    * - __s32
      - ``bottom_field_order_cnt``
      -
    * - __u32
      - ``flags``
      - See :ref:`DPB Entry Flags <h264_dpb_flags>`

.. raw:: latex

    \normalsize

.. _h264_dpb_flags:

``DPB Entries Flags``

.. raw:: latex

    \small

.. tabularcolumns:: |p{7.7cm}|p{2.1cm}|p{7.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_H264_DPB_ENTRY_FLAG_VALID``
      - 0x00000001
      - The DPB entry is valid (non-empty) and should be considered.
    * - ``V4L2_H264_DPB_ENTRY_FLAG_ACTIVE``
      - 0x00000002
      - The DPB entry is used for reference.
    * - ``V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM``
      - 0x00000004
      - The DPB entry is used for long-term reference.
    * - ``V4L2_H264_DPB_ENTRY_FLAG_FIELD``
      - 0x00000008
      - The DPB entry is a single field or a complementary field pair.

.. raw:: latex

    \normalsize

``V4L2_CID_STATELESS_H264_DECODE_MODE (enum)``
    Specifies the decoding mode to use. Currently exposes slice-based and
    frame-based decoding but new modes might be added later on.
    This control is used as a modifier for V4L2_PIX_FMT_H264_SLICE
    pixel format. Applications that support V4L2_PIX_FMT_H264_SLICE
    are required to set this control in order to specify the decoding mode
    that is expected for the buffer.
    Drivers may expose a single or multiple decoding modes, depending
    on what they can support.

.. c:type:: v4l2_stateless_h264_decode_mode

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{7.4cm}|p{0.3cm}|p{9.6cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_STATELESS_H264_DECODE_MODE_SLICE_BASED``
      - 0
      - Decoding is done at the slice granularity.
        The OUTPUT buffer must contain a single slice.
        When this mode is selected, the ``V4L2_CID_STATELESS_H264_SLICE_PARAMS``
        control shall be set. When multiple slices compose a frame,
        use of ``V4L2_BUF_CAP_SUPPORTS_M2M_HOLD_CAPTURE_BUF`` flag
        is required.
    * - ``V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED``
      - 1
      - Decoding is done at the frame granularity,
        The OUTPUT buffer must contain all slices needed to decode the
        frame. The OUTPUT buffer must also contain both fields.
        This mode will be supported by devices that
        parse the slice(s) header(s) in hardware. When this mode is
        selected, the ``V4L2_CID_STATELESS_H264_SLICE_PARAMS``
        control shall not be set.

.. raw:: latex

    \normalsize

``V4L2_CID_STATELESS_H264_START_CODE (enum)``
    Specifies the H264 slice start code expected for each slice.
    This control is used as a modifier for V4L2_PIX_FMT_H264_SLICE
    pixel format. Applications that support V4L2_PIX_FMT_H264_SLICE
    are required to set this control in order to specify the start code
    that is expected for the buffer.
    Drivers may expose a single or multiple start codes, depending
    on what they can support.

.. c:type:: v4l2_stateless_h264_start_code

.. raw:: latex

    \small

.. tabularcolumns:: |p{7.9cm}|p{0.4cm}|p{9.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       4 1 4

    * - ``V4L2_STATELESS_H264_START_CODE_NONE``
      - 0
      - Selecting this value specifies that H264 slices are passed
        to the driver without any start code. The bitstream data should be
        according to :ref:`h264` 7.3.1 NAL unit syntax, hence contains
        emulation prevention bytes when required.
    * - ``V4L2_STATELESS_H264_START_CODE_ANNEX_B``
      - 1
      - Selecting this value specifies that H264 slices are expected
        to be prefixed by Annex B start codes. According to :ref:`h264`
        valid start codes can be 3-bytes 0x000001 or 4-bytes 0x00000001.

.. raw:: latex

    \normalsize

.. _codec-stateless-fwht:

``V4L2_CID_STATELESS_FWHT_PARAMS (struct)``
    Specifies the FWHT (Fast Walsh Hadamard Transform) parameters (as extracted
    from the bitstream) for the associated FWHT data. This includes the necessary
    parameters for configuring a stateless hardware decoding pipeline for FWHT.
    This codec is specific to the vicodec test driver.

.. c:type:: v4l2_ctrl_fwht_params

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.4cm}|p{3.9cm}|p{12.0cm}|

.. flat-table:: struct v4l2_ctrl_fwht_params
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u64
      - ``backward_ref_ts``
      - Timestamp of the V4L2 capture buffer to use as backward reference, used
        with P-coded frames. The timestamp refers to the
	``timestamp`` field in struct :c:type:`v4l2_buffer`. Use the
	:c:func:`v4l2_timeval_to_ns()` function to convert the struct
	:c:type:`timeval` in struct :c:type:`v4l2_buffer` to a __u64.
    * - __u32
      - ``version``
      - The version of the codec. Set to ``V4L2_FWHT_VERSION``.
    * - __u32
      - ``width``
      - The width of the frame.
    * - __u32
      - ``height``
      - The height of the frame.
    * - __u32
      - ``flags``
      - The flags of the frame, see :ref:`fwht-flags`.
    * - __u32
      - ``colorspace``
      - The colorspace of the frame, from enum :c:type:`v4l2_colorspace`.
    * - __u32
      - ``xfer_func``
      - The transfer function, from enum :c:type:`v4l2_xfer_func`.
    * - __u32
      - ``ycbcr_enc``
      - The Y'CbCr encoding, from enum :c:type:`v4l2_ycbcr_encoding`.
    * - __u32
      - ``quantization``
      - The quantization range, from enum :c:type:`v4l2_quantization`.

.. raw:: latex

    \normalsize

.. _fwht-flags:

FWHT Flags
==========

.. raw:: latex

    \small

.. tabularcolumns:: |p{7.0cm}|p{2.3cm}|p{8.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_FWHT_FL_IS_INTERLACED``
      - 0x00000001
      - Set if this is an interlaced format.
    * - ``V4L2_FWHT_FL_IS_BOTTOM_FIRST``
      - 0x00000002
      - Set if this is a bottom-first (NTSC) interlaced format.
    * - ``V4L2_FWHT_FL_IS_ALTERNATE``
      - 0x00000004
      - Set if each 'frame' contains just one field.
    * - ``V4L2_FWHT_FL_IS_BOTTOM_FIELD``
      - 0x00000008
      - If V4L2_FWHT_FL_IS_ALTERNATE was set, then this is set if this 'frame' is the
	bottom field, else it is the top field.
    * - ``V4L2_FWHT_FL_LUMA_IS_UNCOMPRESSED``
      - 0x00000010
      - Set if the Y' (luma) plane is uncompressed.
    * - ``V4L2_FWHT_FL_CB_IS_UNCOMPRESSED``
      - 0x00000020
      - Set if the Cb plane is uncompressed.
    * - ``V4L2_FWHT_FL_CR_IS_UNCOMPRESSED``
      - 0x00000040
      - Set if the Cr plane is uncompressed.
    * - ``V4L2_FWHT_FL_CHROMA_FULL_HEIGHT``
      - 0x00000080
      - Set if the chroma plane has the same height as the luma plane,
	else the chroma plane is half the height of the luma plane.
    * - ``V4L2_FWHT_FL_CHROMA_FULL_WIDTH``
      - 0x00000100
      - Set if the chroma plane has the same width as the luma plane,
	else the chroma plane is half the width of the luma plane.
    * - ``V4L2_FWHT_FL_ALPHA_IS_UNCOMPRESSED``
      - 0x00000200
      - Set if the alpha plane is uncompressed.
    * - ``V4L2_FWHT_FL_I_FRAME``
      - 0x00000400
      - Set if this is an I-frame.
    * - ``V4L2_FWHT_FL_COMPONENTS_NUM_MSK``
      - 0x00070000
      - The number of color components minus one.
    * - ``V4L2_FWHT_FL_PIXENC_MSK``
      - 0x00180000
      - The mask for the pixel encoding.
    * - ``V4L2_FWHT_FL_PIXENC_YUV``
      - 0x00080000
      - Set if the pixel encoding is YUV.
    * - ``V4L2_FWHT_FL_PIXENC_RGB``
      - 0x00100000
      - Set if the pixel encoding is RGB.
    * - ``V4L2_FWHT_FL_PIXENC_HSV``
      - 0x00180000
      - Set if the pixel encoding is HSV.

.. raw:: latex

    \normalsize

.. _v4l2-codec-stateless-vp8:

``V4L2_CID_STATELESS_VP8_FRAME (struct)``
    Specifies the frame parameters for the associated VP8 parsed frame data.
    This includes the necessary parameters for
    configuring a stateless hardware decoding pipeline for VP8.
    The bitstream parameters are defined according to :ref:`vp8`.

.. c:type:: v4l2_ctrl_vp8_frame

.. raw:: latex

    \small

.. tabularcolumns:: |p{7.0cm}|p{4.6cm}|p{5.7cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_vp8_frame
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - struct :c:type:`v4l2_vp8_segment`
      - ``segment``
      - Structure with segment-based adjustments metadata.
    * - struct :c:type:`v4l2_vp8_loop_filter`
      - ``lf``
      - Structure with loop filter level adjustments metadata.
    * - struct :c:type:`v4l2_vp8_quantization`
      - ``quant``
      - Structure with VP8 dequantization indices metadata.
    * - struct :c:type:`v4l2_vp8_entropy`
      - ``entropy``
      - Structure with VP8 entropy coder probabilities metadata.
    * - struct :c:type:`v4l2_vp8_entropy_coder_state`
      - ``coder_state``
      - Structure with VP8 entropy coder state.
    * - __u16
      - ``width``
      - The width of the frame. Must be set for all frames.
    * - __u16
      - ``height``
      - The height of the frame. Must be set for all frames.
    * - __u8
      - ``horizontal_scale``
      - Horizontal scaling factor.
    * - __u8
      - ``vertical_scaling factor``
      - Vertical scale.
    * - __u8
      - ``version``
      - Bitstream version.
    * - __u8
      - ``prob_skip_false``
      - Indicates the probability that the macroblock is not skipped.
    * - __u8
      - ``prob_intra``
      - Indicates the probability that a macroblock is intra-predicted.
    * - __u8
      - ``prob_last``
      - Indicates the probability that the last reference frame is used
        for inter-prediction
    * - __u8
      - ``prob_gf``
      - Indicates the probability that the golden reference frame is used
        for inter-prediction
    * - __u8
      - ``num_dct_parts``
      - Number of DCT coefficients partitions. Must be one of: 1, 2, 4, or 8.
    * - __u32
      - ``first_part_size``
      - Size of the first partition, i.e. the control partition.
    * - __u32
      - ``first_part_header_bits``
      - Size in bits of the first partition header portion.
    * - __u32
      - ``dct_part_sizes[8]``
      - DCT coefficients sizes.
    * - __u64
      - ``last_frame_ts``
      - Timestamp for the V4L2 capture buffer to use as last reference frame, used
        with inter-coded frames. The timestamp refers to the ``timestamp`` field in
	struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
	function to convert the struct :c:type:`timeval` in struct
	:c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``golden_frame_ts``
      - Timestamp for the V4L2 capture buffer to use as last reference frame, used
        with inter-coded frames. The timestamp refers to the ``timestamp`` field in
	struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
	function to convert the struct :c:type:`timeval` in struct
	:c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``alt_frame_ts``
      - Timestamp for the V4L2 capture buffer to use as alternate reference frame, used
        with inter-coded frames. The timestamp refers to the ``timestamp`` field in
	struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
	function to convert the struct :c:type:`timeval` in struct
	:c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``flags``
      - See :ref:`Frame Flags <vp8_frame_flags>`

.. raw:: latex

    \normalsize

.. _vp8_frame_flags:

``Frame Flags``

.. tabularcolumns:: |p{9.8cm}|p{0.8cm}|p{6.7cm}|

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP8_FRAME_FLAG_KEY_FRAME``
      - 0x01
      - Indicates if the frame is a key frame.
    * - ``V4L2_VP8_FRAME_FLAG_EXPERIMENTAL``
      - 0x02
      - Experimental bitstream.
    * - ``V4L2_VP8_FRAME_FLAG_SHOW_FRAME``
      - 0x04
      - Show frame flag, indicates if the frame is for display.
    * - ``V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF``
      - 0x08
      - Enable/disable skipping of macroblocks with no non-zero coefficients.
    * - ``V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN``
      - 0x10
      - Sign of motion vectors when the golden frame is referenced.
    * - ``V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT``
      - 0x20
      - Sign of motion vectors when the alt frame is referenced.

.. c:type:: v4l2_vp8_entropy_coder_state

.. cssclass:: longtable

.. tabularcolumns:: |p{1.0cm}|p{2.0cm}|p{14.3cm}|

.. flat-table:: struct v4l2_vp8_entropy_coder_state
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``range``
      - coder state value for "Range"
    * - __u8
      - ``value``
      - coder state value for "Value"-
    * - __u8
      - ``bit_count``
      - number of bits left.
    * - __u8
      - ``padding``
      - Applications and drivers must set this to zero.

.. c:type:: v4l2_vp8_segment

.. cssclass:: longtable

.. tabularcolumns:: |p{1.2cm}|p{4.0cm}|p{12.1cm}|

.. flat-table:: struct v4l2_vp8_segment
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s8
      - ``quant_update[4]``
      - Signed quantizer value update.
    * - __s8
      - ``lf_update[4]``
      - Signed loop filter level value update.
    * - __u8
      - ``segment_probs[3]``
      - Segment probabilities.
    * - __u8
      - ``padding``
      - Applications and drivers must set this to zero.
    * - __u32
      - ``flags``
      - See :ref:`Segment Flags <vp8_segment_flags>`

.. _vp8_segment_flags:

``Segment Flags``

.. raw:: latex

    \small

.. tabularcolumns:: |p{10cm}|p{1.0cm}|p{6.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP8_SEGMENT_FLAG_ENABLED``
      - 0x01
      - Enable/disable segment-based adjustments.
    * - ``V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP``
      - 0x02
      - Indicates if the macroblock segmentation map is updated in this frame.
    * - ``V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA``
      - 0x04
      - Indicates if the segment feature data is updated in this frame.
    * - ``V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE``
      - 0x08
      - If is set, the segment feature data mode is delta-value.
        If cleared, it's absolute-value.

.. raw:: latex

    \normalsize

.. c:type:: v4l2_vp8_loop_filter

.. cssclass:: longtable

.. tabularcolumns:: |p{1.5cm}|p{3.9cm}|p{11.9cm}|

.. flat-table:: struct v4l2_vp8_loop_filter
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s8
      - ``ref_frm_delta[4]``
      - Reference adjustment (signed) delta value.
    * - __s8
      - ``mb_mode_delta[4]``
      - Macroblock prediction mode adjustment (signed) delta value.
    * - __u8
      - ``sharpness_level``
      - Sharpness level
    * - __u8
      - ``level``
      - Filter level
    * - __u16
      - ``padding``
      - Applications and drivers must set this to zero.
    * - __u32
      - ``flags``
      - See :ref:`Loop Filter Flags <vp8_loop_filter_flags>`

.. _vp8_loop_filter_flags:

``Loop Filter Flags``

.. tabularcolumns:: |p{7.0cm}|p{1.2cm}|p{9.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP8_LF_ADJ_ENABLE``
      - 0x01
      - Enable/disable macroblock-level loop filter adjustment.
    * - ``V4L2_VP8_LF_DELTA_UPDATE``
      - 0x02
      - Indicates if the delta values used in an adjustment are updated.
    * - ``V4L2_VP8_LF_FILTER_TYPE_SIMPLE``
      - 0x04
      - If set, indicates the filter type is simple.
        If cleared, the filter type is normal.

.. c:type:: v4l2_vp8_quantization

.. tabularcolumns:: |p{1.5cm}|p{3.5cm}|p{12.3cm}|

.. flat-table:: struct v4l2_vp8_quantization
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``y_ac_qi``
      - Luma AC coefficient table index.
    * - __s8
      - ``y_dc_delta``
      - Luma DC delta vaue.
    * - __s8
      - ``y2_dc_delta``
      - Y2 block DC delta value.
    * - __s8
      - ``y2_ac_delta``
      - Y2 block AC delta value.
    * - __s8
      - ``uv_dc_delta``
      - Chroma DC delta value.
    * - __s8
      - ``uv_ac_delta``
      - Chroma AC delta value.
    * - __u16
      - ``padding``
      - Applications and drivers must set this to zero.

.. c:type:: v4l2_vp8_entropy

.. cssclass:: longtable

.. tabularcolumns:: |p{1.5cm}|p{5.8cm}|p{10.0cm}|

.. flat-table:: struct v4l2_vp8_entropy
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``coeff_probs[4][8][3][11]``
      - Coefficient update probabilities.
    * - __u8
      - ``y_mode_probs[4]``
      - Luma mode update probabilities.
    * - __u8
      - ``uv_mode_probs[3]``
      - Chroma mode update probabilities.
    * - __u8
      - ``mv_probs[2][19]``
      - MV decoding update probabilities.
    * - __u8
      - ``padding[3]``
      - Applications and drivers must set this to zero.

.. _v4l2-codec-stateless-mpeg2:

``V4L2_CID_STATELESS_MPEG2_SEQUENCE (struct)``
    Specifies the sequence parameters (as extracted from the bitstream) for the
    associated MPEG-2 slice data. This includes fields matching the syntax
    elements from the sequence header and sequence extension parts of the
    bitstream as specified by :ref:`mpeg2part2`.

.. c:type:: v4l2_ctrl_mpeg2_sequence

.. raw:: latex

    \small

.. cssclass:: longtable

.. tabularcolumns:: |p{1.4cm}|p{6.5cm}|p{9.4cm}|

.. flat-table:: struct v4l2_ctrl_mpeg2_sequence
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u16
      - ``horizontal_size``
      - The width of the displayable part of the frame's luminance component.
    * - __u16
      - ``vertical_size``
      - The height of the displayable part of the frame's luminance component.
    * - __u32
      - ``vbv_buffer_size``
      - Used to calculate the required size of the video buffering verifier,
	defined (in bits) as: 16 * 1024 * vbv_buffer_size.
    * - __u16
      - ``profile_and_level_indication``
      - The current profile and level indication as extracted from the
	bitstream.
    * - __u8
      - ``chroma_format``
      - The chrominance sub-sampling format (1: 4:2:0, 2: 4:2:2, 3: 4:4:4).
    * - __u8
      - ``flags``
      - See :ref:`MPEG-2 Sequence Flags <mpeg2_sequence_flags>`.

.. _mpeg2_sequence_flags:

``MPEG-2 Sequence Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_MPEG2_SEQ_FLAG_PROGRESSIVE``
      - 0x01
      - Indication that all the frames for the sequence are progressive instead
	of interlaced.

.. raw:: latex

    \normalsize

``V4L2_CID_STATELESS_MPEG2_PICTURE (struct)``
    Specifies the picture parameters (as extracted from the bitstream) for the
    associated MPEG-2 slice data. This includes fields matching the syntax
    elements from the picture header and picture coding extension parts of the
    bitstream as specified by :ref:`mpeg2part2`.

.. c:type:: v4l2_ctrl_mpeg2_picture

.. raw:: latex

    \small

.. cssclass:: longtable

.. tabularcolumns:: |p{1.0cm}|p{5.6cm}|p{10.7cm}|

.. flat-table:: struct v4l2_ctrl_mpeg2_picture
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u64
      - ``backward_ref_ts``
      - Timestamp of the V4L2 capture buffer to use as backward reference, used
        with B-coded and P-coded frames. The timestamp refers to the
	``timestamp`` field in struct :c:type:`v4l2_buffer`. Use the
	:c:func:`v4l2_timeval_to_ns()` function to convert the struct
	:c:type:`timeval` in struct :c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``forward_ref_ts``
      - Timestamp for the V4L2 capture buffer to use as forward reference, used
        with B-coded frames. The timestamp refers to the ``timestamp`` field in
	struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
	function to convert the struct :c:type:`timeval` in struct
	:c:type:`v4l2_buffer` to a __u64.
    * - __u32
      - ``flags``
      - See :ref:`MPEG-2 Picture Flags <mpeg2_picture_flags>`.
    * - __u8
      - ``f_code[2][2]``
      - Motion vector codes.
    * - __u8
      - ``picture_coding_type``
      - Picture coding type for the frame covered by the current slice
	(V4L2_MPEG2_PIC_CODING_TYPE_I, V4L2_MPEG2_PIC_CODING_TYPE_P or
	V4L2_MPEG2_PIC_CODING_TYPE_B).
    * - __u8
      - ``picture_structure``
      - Picture structure (1: interlaced top field, 2: interlaced bottom field,
	3: progressive frame).
    * - __u8
      - ``intra_dc_precision``
      - Precision of Discrete Cosine transform (0: 8 bits precision,
	1: 9 bits precision, 2: 10 bits precision, 3: 11 bits precision).
    * - __u8
      - ``reserved[5]``
      - Applications and drivers must set this to zero.

.. _mpeg2_picture_flags:

``MPEG-2 Picture Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST``
      - 0x00000001
      - If set and it's an interlaced stream, top field is output first.
    * - ``V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT``
      - 0x00000002
      - If set only frame-DCT and frame prediction are used.
    * - ``V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV``
      - 0x00000004
      -  If set motion vectors are coded for intra macroblocks.
    * - ``V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE``
      - 0x00000008
      - This flag affects the inverse quantization process.
    * - ``V4L2_MPEG2_PIC_FLAG_INTRA_VLC``
      - 0x00000010
      - This flag affects the decoding of transform coefficient data.
    * - ``V4L2_MPEG2_PIC_FLAG_ALT_SCAN``
      - 0x00000020
      - This flag affects the decoding of transform coefficient data.
    * - ``V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST``
      - 0x00000040
      - This flag affects the decoding process of progressive frames.
    * - ``V4L2_MPEG2_PIC_FLAG_PROGRESSIVE``
      - 0x00000080
      - Indicates whether the current frame is progressive.

.. raw:: latex

    \normalsize

``V4L2_CID_STATELESS_MPEG2_QUANTISATION (struct)``
    Specifies quantisation matrices, in zigzag scanning order, for the
    associated MPEG-2 slice data. This control is initialized by the kernel
    to the matrices default values. If a bitstream transmits a user-defined
    quantisation matrices load, applications are expected to use this control.
    Applications are also expected to set the control loading the default
    values, if the quantisation matrices need to be reset, for instance on a
    sequence header. This process is specified by section 6.3.7.
    "Quant matrix extension" of the specification.

.. c:type:: v4l2_ctrl_mpeg2_quantisation

.. tabularcolumns:: |p{0.8cm}|p{8.0cm}|p{8.5cm}|

.. cssclass:: longtable

.. raw:: latex

    \small

.. flat-table:: struct v4l2_ctrl_mpeg2_quantisation
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``intra_quantiser_matrix[64]``
      - The quantisation matrix coefficients for intra-coded frames, in zigzag
	scanning order. It is relevant for both luma and chroma components,
	although it can be superseded by the chroma-specific matrix for
	non-4:2:0 YUV formats.
    * - __u8
      - ``non_intra_quantiser_matrix[64]``
      - The quantisation matrix coefficients for non-intra-coded frames, in
	zigzag scanning order. It is relevant for both luma and chroma
	components, although it can be superseded by the chroma-specific matrix
	for non-4:2:0 YUV formats.
    * - __u8
      - ``chroma_intra_quantiser_matrix[64]``
      - The quantisation matrix coefficients for the chominance component of
	intra-coded frames, in zigzag scanning order. Only relevant for
	non-4:2:0 YUV formats.
    * - __u8
      - ``chroma_non_intra_quantiser_matrix[64]``
      - The quantisation matrix coefficients for the chrominance component of
	non-intra-coded frames, in zigzag scanning order. Only relevant for
	non-4:2:0 YUV formats.

.. raw:: latex

    \normalsize

.. _v4l2-codec-stateless-vp9:

``V4L2_CID_STATELESS_VP9_COMPRESSED_HDR (struct)``
    Stores VP9 probabilities updates as parsed from the current compressed frame
    header. A value of zero in an array element means no update of the relevant
    probability. Motion vector-related updates contain a new value or zero. All
    other updates contain values translated with inv_map_table[] (see 6.3.5 in
    :ref:`vp9`).

.. c:type:: v4l2_ctrl_vp9_compressed_hdr

.. tabularcolumns:: |p{1cm}|p{4.8cm}|p{11.4cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_vp9_compressed_hdr
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``tx_mode``
      - Specifies the TX mode. See :ref:`TX Mode <vp9_tx_mode>` for more details.
    * - __u8
      - ``tx8[2][1]``
      - TX 8x8 probabilities delta.
    * - __u8
      - ``tx16[2][2]``
      - TX 16x16 probabilities delta.
    * - __u8
      - ``tx32[2][3]``
      - TX 32x32 probabilities delta.
    * - __u8
      - ``coef[4][2][2][6][6][3]``
      - Coefficient probabilities delta.
    * - __u8
      - ``skip[3]``
      - Skip probabilities delta.
    * - __u8
      - ``inter_mode[7][3]``
      - Inter prediction mode probabilities delta.
    * - __u8
      - ``interp_filter[4][2]``
      - Interpolation filter probabilities delta.
    * - __u8
      - ``is_inter[4]``
      - Is inter-block probabilities delta.
    * - __u8
      - ``comp_mode[5]``
      - Compound prediction mode probabilities delta.
    * - __u8
      - ``single_ref[5][2]``
      - Single reference probabilities delta.
    * - __u8
      - ``comp_ref[5]``
      - Compound reference probabilities delta.
    * - __u8
      - ``y_mode[4][9]``
      - Y prediction mode probabilities delta.
    * - __u8
      - ``uv_mode[10][9]``
      - UV prediction mode probabilities delta.
    * - __u8
      - ``partition[16][3]``
      - Partition probabilities delta.
    * - __u8
      - ``mv.joint[3]``
      - Motion vector joint probabilities delta.
    * - __u8
      - ``mv.sign[2]``
      - Motion vector sign probabilities delta.
    * - __u8
      - ``mv.classes[2][10]``
      - Motion vector class probabilities delta.
    * - __u8
      - ``mv.class0_bit[2]``
      - Motion vector class0 bit probabilities delta.
    * - __u8
      - ``mv.bits[2][10]``
      - Motion vector bits probabilities delta.
    * - __u8
      - ``mv.class0_fr[2][2][3]``
      - Motion vector class0 fractional bit probabilities delta.
    * - __u8
      - ``mv.fr[2][3]``
      - Motion vector fractional bit probabilities delta.
    * - __u8
      - ``mv.class0_hp[2]``
      - Motion vector class0 high precision fractional bit probabilities delta.
    * - __u8
      - ``mv.hp[2]``
      - Motion vector high precision fractional bit probabilities delta.

.. _vp9_tx_mode:

``TX Mode``

.. tabularcolumns:: |p{6.5cm}|p{0.5cm}|p{10.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_TX_MODE_ONLY_4X4``
      - 0
      - Transform size is 4x4.
    * - ``V4L2_VP9_TX_MODE_ALLOW_8X8``
      - 1
      - Transform size can be up to 8x8.
    * - ``V4L2_VP9_TX_MODE_ALLOW_16X16``
      - 2
      - Transform size can be up to 16x16.
    * - ``V4L2_VP9_TX_MODE_ALLOW_32X32``
      - 3
      - transform size can be up to 32x32.
    * - ``V4L2_VP9_TX_MODE_SELECT``
      - 4
      - Bitstream contains the transform size for each block.

See section '7.3.1 Tx mode semantics' of the :ref:`vp9` specification for more details.

``V4L2_CID_STATELESS_VP9_FRAME (struct)``
    Specifies the frame parameters for the associated VP9 frame decode request.
    This includes the necessary parameters for configuring a stateless hardware
    decoding pipeline for VP9. The bitstream parameters are defined according
    to :ref:`vp9`.

.. c:type:: v4l2_ctrl_vp9_frame

.. raw:: latex

    \small

.. tabularcolumns:: |p{4.7cm}|p{5.5cm}|p{7.1cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_vp9_frame
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - struct :c:type:`v4l2_vp9_loop_filter`
      - ``lf``
      - Loop filter parameters. See struct :c:type:`v4l2_vp9_loop_filter` for more details.
    * - struct :c:type:`v4l2_vp9_quantization`
      - ``quant``
      - Quantization parameters. See :c:type:`v4l2_vp9_quantization` for more details.
    * - struct :c:type:`v4l2_vp9_segmentation`
      - ``seg``
      - Segmentation parameters. See :c:type:`v4l2_vp9_segmentation` for more details.
    * - __u32
      - ``flags``
      - Combination of V4L2_VP9_FRAME_FLAG_* flags. See :ref:`Frame Flags<vp9_frame_flags>`.
    * - __u16
      - ``compressed_header_size``
      - Compressed header size in bytes.
    * - __u16
      - ``uncompressed_header_size``
      - Uncompressed header size in bytes.
    * - __u16
      - ``frame_width_minus_1``
      - Add 1 to get the frame width expressed in pixels. See section 7.2.3 in :ref:`vp9`.
    * - __u16
      - ``frame_height_minus_1``
      - Add 1 to get the frame height expressed in pixels. See section 7.2.3 in :ref:`vp9`.
    * - __u16
      - ``render_width_minus_1``
      - Add 1 to get the expected render width expressed in pixels. This is
        not used during the decoding process but might be used by HW scalers to
        prepare a frame that's ready for scanout. See section 7.2.4 in :ref:`vp9`.
    * - __u16
      - render_height_minus_1
      - Add 1 to get the expected render height expressed in pixels. This is
        not used during the decoding process but might be used by HW scalers to
        prepare a frame that's ready for scanout. See section 7.2.4 in :ref:`vp9`.
    * - __u64
      - ``last_frame_ts``
      - "last" reference buffer timestamp.
	The timestamp refers to the ``timestamp`` field in
        struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
        function to convert the struct :c:type:`timeval` in struct
        :c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``golden_frame_ts``
      - "golden" reference buffer timestamp.
	The timestamp refers to the ``timestamp`` field in
        struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
        function to convert the struct :c:type:`timeval` in struct
        :c:type:`v4l2_buffer` to a __u64.
    * - __u64
      - ``alt_frame_ts``
      - "alt" reference buffer timestamp.
	The timestamp refers to the ``timestamp`` field in
        struct :c:type:`v4l2_buffer`. Use the :c:func:`v4l2_timeval_to_ns()`
        function to convert the struct :c:type:`timeval` in struct
        :c:type:`v4l2_buffer` to a __u64.
    * - __u8
      - ``ref_frame_sign_bias``
      - a bitfield specifying whether the sign bias is set for a given
        reference frame. See :ref:`Reference Frame Sign Bias<vp9_ref_frame_sign_bias>`
        for more details.
    * - __u8
      - ``reset_frame_context``
      - specifies whether the frame context should be reset to default values. See
        :ref:`Reset Frame Context<vp9_reset_frame_context>` for more details.
    * - __u8
      - ``frame_context_idx``
      - Frame context that should be used/updated.
    * - __u8
      - ``profile``
      - VP9 profile. Can be 0, 1, 2 or 3.
    * - __u8
      - ``bit_depth``
      - Component depth in bits. Can be 8, 10 or 12. Note that not all profiles
        support 10 and/or 12 bits depths.
    * - __u8
      - ``interpolation_filter``
      - Specifies the filter selection used for performing inter prediction. See
        :ref:`Interpolation Filter<vp9_interpolation_filter>` for more details.
    * - __u8
      - ``tile_cols_log2``
      - Specifies the base 2 logarithm of the width of each tile (where the
        width is measured in units of 8x8 blocks). Shall be less than or equal
        to 6.
    * - __u8
      - ``tile_rows_log2``
      - Specifies the base 2 logarithm of the height of each tile (where the
        height is measured in units of 8x8 blocks).
    * - __u8
      - ``reference_mode``
      - Specifies the type of inter prediction to be used. See
        :ref:`Reference Mode<vp9_reference_mode>` for more details.
    * - __u8
      - ``reserved[7]``
      - Applications and drivers must set this to zero.

.. raw:: latex

    \normalsize

.. _vp9_frame_flags:

``Frame Flags``

.. tabularcolumns:: |p{10.0cm}|p{1.2cm}|p{6.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_FRAME_FLAG_KEY_FRAME``
      - 0x001
      - The frame is a key frame.
    * - ``V4L2_VP9_FRAME_FLAG_SHOW_FRAME``
      - 0x002
      - The frame should be displayed.
    * - ``V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT``
      - 0x004
      - The decoding should be error resilient.
    * - ``V4L2_VP9_FRAME_FLAG_INTRA_ONLY``
      - 0x008
      - The frame does not reference other frames.
    * - ``V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV``
      - 0x010
      - The frame can use high precision motion vectors.
    * - ``V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX``
      - 0x020
      - Frame context should be updated after decoding.
    * - ``V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE``
      - 0x040
      - Parallel decoding is used.
    * - ``V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING``
      - 0x080
      - Vertical subsampling is enabled.
    * - ``V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING``
      - 0x100
      - Horizontal subsampling is enabled.
    * - ``V4L2_VP9_FRAME_FLAG_COLOR_RANGE_FULL_SWING``
      - 0x200
      - The full UV range is used.

.. _vp9_ref_frame_sign_bias:

``Reference Frame Sign Bias``

.. tabularcolumns:: |p{7.0cm}|p{1.2cm}|p{9.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_SIGN_BIAS_LAST``
      - 0x1
      - Sign bias is set for the last reference frame.
    * - ``V4L2_VP9_SIGN_BIAS_GOLDEN``
      - 0x2
      - Sign bias is set for the golden reference frame.
    * - ``V4L2_VP9_SIGN_BIAS_ALT``
      - 0x2
      - Sign bias is set for the alt reference frame.

.. _vp9_reset_frame_context:

``Reset Frame Context``

.. tabularcolumns:: |p{7.0cm}|p{1.2cm}|p{9.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_RESET_FRAME_CTX_NONE``
      - 0
      - Do not reset any frame context.
    * - ``V4L2_VP9_RESET_FRAME_CTX_SPEC``
      - 1
      - Reset the frame context pointed to by
        :c:type:`v4l2_ctrl_vp9_frame`.frame_context_idx.
    * - ``V4L2_VP9_RESET_FRAME_CTX_ALL``
      - 2
      - Reset all frame contexts.

See section '7.2 Uncompressed header semantics' of the :ref:`vp9` specification
for more details.

.. _vp9_interpolation_filter:

``Interpolation Filter``

.. tabularcolumns:: |p{9.0cm}|p{1.2cm}|p{7.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_INTERP_FILTER_EIGHTTAP``
      - 0
      - Eight tap filter.
    * - ``V4L2_VP9_INTERP_FILTER_EIGHTTAP_SMOOTH``
      - 1
      - Eight tap smooth filter.
    * - ``V4L2_VP9_INTERP_FILTER_EIGHTTAP_SHARP``
      - 2
      - Eeight tap sharp filter.
    * - ``V4L2_VP9_INTERP_FILTER_BILINEAR``
      - 3
      - Bilinear filter.
    * - ``V4L2_VP9_INTERP_FILTER_SWITCHABLE``
      - 4
      - Filter selection is signaled at the block level.

See section '7.2.7 Interpolation filter semantics' of the :ref:`vp9` specification
for more details.

.. _vp9_reference_mode:

``Reference Mode``

.. tabularcolumns:: |p{9.6cm}|p{0.5cm}|p{7.2cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_REFERENCE_MODE_SINGLE_REFERENCE``
      - 0
      - Indicates that all the inter blocks use only a single reference frame
        to generate motion compensated prediction.
    * - ``V4L2_VP9_REFERENCE_MODE_COMPOUND_REFERENCE``
      - 1
      - Requires all the inter blocks to use compound mode. Single reference
        frame prediction is not allowed.
    * - ``V4L2_VP9_REFERENCE_MODE_SELECT``
      - 2
      - Allows each individual inter block to select between single and
        compound prediction modes.

See section '7.3.6 Frame reference mode semantics' of the :ref:`vp9` specification for more details.

.. c:type:: v4l2_vp9_segmentation

Encodes the quantization parameters. See section '7.2.10 Segmentation
params syntax' of the :ref:`vp9` specification for more details.

.. tabularcolumns:: |p{0.8cm}|p{5cm}|p{11.4cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_vp9_segmentation
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``feature_data[8][4]``
      - Data attached to each feature. Data entry is only valid if the feature
        is enabled. The array shall be indexed with segment number as the first dimension
        (0..7) and one of V4L2_VP9_SEG_* as the second dimension.
        See :ref:`Segment Feature IDs<vp9_segment_feature>`.
    * - __u8
      - ``feature_enabled[8]``
      - Bitmask defining which features are enabled in each segment. The value for each
        segment is a combination of V4L2_VP9_SEGMENT_FEATURE_ENABLED(id) values where id is
        one of V4L2_VP9_SEG_*. See :ref:`Segment Feature IDs<vp9_segment_feature>`.
    * - __u8
      - ``tree_probs[7]``
      - Specifies the probability values to be used when decoding a Segment-ID.
        See '5.15. Segmentation map' section of :ref:`vp9` for more details.
    * - __u8
      - ``pred_probs[3]``
      - Specifies the probability values to be used when decoding a
        Predicted-Segment-ID. See '6.4.14. Get segment id syntax'
        section of :ref:`vp9` for more details.
    * - __u8
      - ``flags``
      - Combination of V4L2_VP9_SEGMENTATION_FLAG_* flags. See
        :ref:`Segmentation Flags<vp9_segmentation_flags>`.
    * - __u8
      - ``reserved[5]``
      - Applications and drivers must set this to zero.

.. _vp9_segment_feature:

``Segment feature IDs``

.. tabularcolumns:: |p{6.0cm}|p{1cm}|p{10.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_SEG_LVL_ALT_Q``
      - 0
      - Quantizer segment feature.
    * - ``V4L2_VP9_SEG_LVL_ALT_L``
      - 1
      - Loop filter segment feature.
    * - ``V4L2_VP9_SEG_LVL_REF_FRAME``
      - 2
      - Reference frame segment feature.
    * - ``V4L2_VP9_SEG_LVL_SKIP``
      - 3
      - Skip segment feature.
    * - ``V4L2_VP9_SEG_LVL_MAX``
      - 4
      - Number of segment features.

.. _vp9_segmentation_flags:

``Segmentation Flags``

.. tabularcolumns:: |p{10.6cm}|p{0.8cm}|p{5.9cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_SEGMENTATION_FLAG_ENABLED``
      - 0x01
      - Indicates that this frame makes use of the segmentation tool.
    * - ``V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP``
      - 0x02
      - Indicates that the segmentation map should be updated during the
        decoding of this frame.
    * - ``V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE``
      - 0x04
      - Indicates that the updates to the segmentation map are coded
        relative to the existing segmentation map.
    * - ``V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA``
      - 0x08
      - Indicates that new parameters are about to be specified for each
        segment.
    * - ``V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE``
      - 0x10
      - Indicates that the segmentation parameters represent the actual values
        to be used.

.. c:type:: v4l2_vp9_quantization

Encodes the quantization parameters. See section '7.2.9 Quantization params
syntax' of the VP9 specification for more details.

.. tabularcolumns:: |p{0.8cm}|p{4cm}|p{12.4cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_vp9_quantization
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``base_q_idx``
      - Indicates the base frame qindex.
    * - __s8
      - ``delta_q_y_dc``
      - Indicates the Y DC quantizer relative to base_q_idx.
    * - __s8
      - ``delta_q_uv_dc``
      - Indicates the UV DC quantizer relative to base_q_idx.
    * - __s8
      - ``delta_q_uv_ac``
      - Indicates the UV AC quantizer relative to base_q_idx.
    * - __u8
      - ``reserved[4]``
      - Applications and drivers must set this to zero.

.. c:type:: v4l2_vp9_loop_filter

This structure contains all loop filter related parameters. See sections
'7.2.8 Loop filter semantics' of the :ref:`vp9` specification for more details.

.. tabularcolumns:: |p{0.8cm}|p{4cm}|p{12.4cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_vp9_loop_filter
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s8
      - ``ref_deltas[4]``
      - Contains the adjustment needed for the filter level based on the chosen
        reference frame.
    * - __s8
      - ``mode_deltas[2]``
      - Contains the adjustment needed for the filter level based on the chosen
        mode.
    * - __u8
      - ``level``
      - Indicates the loop filter strength.
    * - __u8
      - ``sharpness``
      - Indicates the sharpness level.
    * - __u8
      - ``flags``
      - Combination of V4L2_VP9_LOOP_FILTER_FLAG_* flags.
        See :ref:`Loop Filter Flags <vp9_loop_filter_flags>`.
    * - __u8
      - ``reserved[7]``
      - Applications and drivers must set this to zero.


.. _vp9_loop_filter_flags:

``Loop Filter Flags``

.. tabularcolumns:: |p{9.6cm}|p{0.5cm}|p{7.2cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED``
      - 0x1
      - When set, the filter level depends on the mode and reference frame used
        to predict a block.
    * - ``V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE``
      - 0x2
      - When set, the bitstream contains additional syntax elements that
        specify which mode and reference frame deltas are to be updated.
