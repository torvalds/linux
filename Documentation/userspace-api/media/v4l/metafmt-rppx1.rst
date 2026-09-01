.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-rppx1-params:
.. _v4l2-meta-fmt-rppx1-stats:

*************************************************************************
V4L2_META_FMT_RPPX1_PARAMS ('DR1P'), V4L2_META_FMT_RPPX1_STATS ('DR1S')
*************************************************************************

Configuration Parameters
========================

The configuration parameters are passed to the metadata output video node, using
the :c:type:`v4l2_meta_format` interface. Rather than a single struct containing
sub-structs for each configurable area of the ISP, parameters for the Dreamchip
RPPX1 use the v4l2-isp parameters system, through which groups of parameters are
defined as distinct structs or "blocks" which may be added to the data member of
:c:type:`v4l2_isp_buffer`. Userspace is responsible for populating the data
member with the blocks that need to be configured by the driver.  Each
block-specific struct embeds :c:type:`v4l2_isp_block_header` as its first member
and userspace must populate the type member with a value from
:c:type:`rppx1_params_block_type`.

.. code-block:: c

	struct v4l2_isp_params_buffer *params =
		(struct v4l2_isp_params_buffer *)buffer;

	params->version = V4L2_ISP_PARAMS_VERSION_V1;
	params->data_size = 0;

	void *data = (void *)params->data;

	struct rppx1_ccor_params *ccor =
		(struct rppx1_ccor_params *)data;

	ccor->header.type = RPPX1_PARAMS_BLOCK_TYPE_CCOR_POST;
	ccor->header.flags |= V4L2_ISP_PARAMS_FL_BLOCK_ENABLE;
	ccor->header.size = sizeof(struct rppx1_ccor_params);

        ccor->coeff[0][0] = 0x1000;
        ccor->coeff[0][1] = 0x0000;
        ccor->coeff[0][2] = 0x0000;
        ccor->coeff[1][0] = 0x0000;
        ccor->coeff[1][1] = 0x1000;
        ccor->coeff[1][2] = 0x0000;
        ccor->coeff[2][0] = 0x0000;
        ccor->coeff[2][1] = 0x0000;
        ccor->coeff[2][2] = 0x1000;

        ccor->offset[0] = 0x200000;
        ccor->offset[1] = 0x200000;
        ccor->offset[2] = 0x200000;

	data += sizeof(struct rppx1_ccor_params);
	params->data_size += sizeof(struct rppx1_ccor_params);

3A Statistics
=============

The ISP device collects different statistics over an input bayer frame. Those
statistics can be obtained by userspace from the metadata capture video node,
using the :c:type:`v4l2_meta_format` interface. Rather than a single struct
containing sub-structs for each statistics area of the ISP, statistics for the
Dreamchip RPPX1 use the v4l2-isp statistics system, through which groups of
statistics are defined as distinct structs or "blocks" which may be added to the
data member of :c:type:`v4l2_isp_buffer`. Userspace is responsible for parsing
the buffer and extracting the blocks of statistics. Each block-specific struct
embeds :c:type:`v4l2_isp_block_header` as its first member and userspace must
interpret the type member with a value from :c:type:`rppx1_stats_block_type`.

.. code-block:: C

        const struct v4l2_isp_buffer *stats =
                (struct v4l2_isp_buffer *)buf;
        size_t block_offset = 0;

        while (block_offset < stats->data_size) {
                const struct v4l2_isp_stats_block_header *block =
                        (void*)(stats->data + block_offset);

                block_offset += block->size;

                switch (block->type) {
                case RPPX1_STATS_BLOCK_TYPE_HIST_POST:
                        for (unsigned int i = 0; i < RPPX1_HIST_NUM_BINS; i++)
                                printf("hist.hist_bins[%u] = 0x%08x\n",
                                        i, hist.hist_bins[%i]);
                        break;
                default:
                        printf("Unknown block type 0x%04x", block->type);
                        break;
                }
        }

Dreamchip RPPX1 uAPI data types
===============================

.. kernel-doc:: include/uapi/linux/media/dreamchip/rppx1-config.h
