/*
 * dpcd_structs.h
 *
 *  Created on: Oct 31, 2018
 *      Author: jlei
 */

#ifndef DAL_INCLUDE_DPCD_STRUCTS_H_
#define DAL_INCLUDE_DPCD_STRUCTS_H_

struct dpcd_receive_port0_cap01 {
	union {
		struct {
			// Byte 0
			unsigned char reserved0				:1; // Bit0
			unsigned char local_edid_present		:1;
			unsigned char associated_to_preceding_port	:1;
			unsigned char hblank_expansion_capable		:1;
			unsigned char buffer_size_unit			:1; // Bit4
			unsigned char buffer_size_per_port		:1;
			unsigned char reserved1				:2;

			// Byte 1
			unsigned char buffer_size			:8;
		} fields;
		unsigned char raw[2];
	};
};

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT

struct dpcd_dsc_basic_capabilities {
	union {
		struct {
			// Byte 0
			struct {
				unsigned char dsc_support		:1; // Bit0
				unsigned char dsc_passthrough_support	:1; // Bit1
				unsigned char reserved			:6;
			} dsc_support;

			// Byte 1
			struct {
				unsigned char dsc_version_major	:4;
				unsigned char dsc_version_minor	:4;
			} dsc_algorithm_revision;

			// Byte 2
			struct {
				unsigned char rc_block_buffer_size	:2;
				unsigned char reserved	:6;
			} dsc_rc_buffer_block_size;

			// Byte 3
			unsigned char dsc_rc_buffer_size;

			// Byte 4
			struct {
				unsigned char one_slice_per_dp_dsc_sink_device		:1; // Bit0
				unsigned char two_slices_per_dp_dsc_sink_device		:1;
				unsigned char reserved					:1;
				unsigned char four_slices_per_dp_dsc_sink_device	:1;
				unsigned char six_slices_per_dp_dsc_sink_device		:1; // Bit 4
				unsigned char eight_slices_per_dp_dsc_sink_device	:1;
				unsigned char ten_slices_per_dp_dsc_sink_device		:1;
				unsigned char twelve_slices_per_dp_dsc_sink_device	:1;
			} dsc_slice_capabilities_1;

			// Byte 5
			struct {
				unsigned char line_buffer_bit_depth	:4;
				unsigned char reserved			:4;
			} dsc_line_buffer_bit_depth;

			// Byte 6
			struct {
				unsigned char block_prediction_support	:1;
				unsigned char reserved			:7;
			} dsc_block_prediction_support;

			// Byte 7,8
			struct {
				unsigned char maximum_bits_per_pixel_supported_by_the_decompressor_low	:7;
				unsigned char maximum_bits_per_pixel_supported_by_the_decompressor_high	:7;
			} maximum_bits_per_pixel_supported_by_the_decompressor;

			// Byte 9
			struct {
				unsigned char rgb_support			:1; // Bit0
				unsigned char y_cb_cr_444_support		:1;
				unsigned char y_cb_cr_simple_422_support	:1;
				unsigned char y_cb_cr_native_422_support	:1;
				unsigned char y_cb_cr_native_420_support	:1; // Bit 4
				unsigned char reserved				:3;
			} dsc_decoder_color_format_capabilities;

			// Byte 10
			struct {
				unsigned char reserved0				:1; // Bit0
				unsigned char eight_bits_per_color_support	:1;
				unsigned char ten_bits_per_color_support	:1;
				unsigned char twelve_bits_per_color_support	:1;
				unsigned char reserved1				:4; // Bit 4
			} dsc_decoder_color_depth_capabilities;

			// Byte 11
			struct {
				unsigned char throughput_mode_0			:4;
				unsigned char throughput_mode_1			:4;
			} peak_dsc_throughput_dsc_sink;

			// Byte 12
			unsigned char dsc_maximum_slice_width;

			// Byte 13
			struct {
				unsigned char sixteen_slices_per_dsc_sink_device	:1;
				unsigned char twenty_slices_per_dsc_sink_device		:1;
				unsigned char twentyfour_slices_per_dsc_sink_device	:1;
				unsigned char reserved					:5;
			} dsc_slice_capabilities_2;

			// Byte 14
			unsigned char reserved;

			// Byte 15
			struct {
				unsigned char increment_of_bits_per_pixel_supported	:3;
				unsigned char reserved					:5;
			} bits_per_pixel_increment;
		} fields;
		unsigned char raw[16];
	};
};

struct dpcd_dsc_ext_capabilities {
	union {
		struct {
			unsigned char branch_overall_throughput_0; // Byte 0
			unsigned char branch_overall_throughput_1; // Byte 1
			unsigned char branch_max_line_width; // Byte 2
		} fields;
		unsigned char raw[3];
	};
};

struct dpcd_dsc_capabilities {
	struct dpcd_dsc_basic_capabilities dsc_basic_caps;
	struct dpcd_dsc_ext_capabilities dsc_ext_caps;
};

struct dpcd_fec_capability {
	union {
		struct {
			// Byte 0
			unsigned char fec_capable				:1; // Bit0
			unsigned char uncorrected_block_error_count_capable	:1;
			unsigned char corrected_block_error_count_capable	:1;
			unsigned char bit_error_count_capable			:1;
			unsigned char reserved					:4; // Bit4
		} fields;
		unsigned char raw[1];
	};
};

#endif

#endif /* DAL_INCLUDE_DPCD_STRUCTS_H_ */
