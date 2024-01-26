/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_STREAM_H
#define PVR_ROGUE_FWIF_STREAM_H

/**
 * DOC: Streams
 *
 * Commands are submitted to the kernel driver in the form of streams.
 *
 * A command stream has the following layout :
 *  - A 64-bit header containing:
 *    * A u32 containing the length of the main stream inclusive of the length of the header.
 *    * A u32 for padding.
 *  - The main stream data.
 *  - The extension stream (optional), which is composed of:
 *    * One or more headers.
 *    * The extension stream data, corresponding to the extension headers.
 *
 * The main stream provides the base command data. This has a fixed layout based on the features
 * supported by a given GPU.
 *
 * The extension stream provides the command parameters that are required for BRNs & ERNs for the
 * current GPU. This stream is comprised of one or more headers, followed by data for each given
 * BRN/ERN.
 *
 * Each header is a u32 containing a bitmask of quirks & enhancements in the extension stream, a
 * "type" field determining the set of quirks & enhancements the bitmask represents, and a
 * continuation bit determining whether any more headers are present. The headers are then followed
 * by command data; this is specific to each quirk/enhancement. All unused / reserved bits in the
 * header must be set to 0.
 *
 * All parameters and headers in the main and extension streams must be naturally aligned.
 *
 * If a parameter appears in both the main and extension streams, then the extension parameter is
 * used.
 */

/*
 * Stream extension header definition
 */
#define PVR_STREAM_EXTHDR_TYPE_SHIFT 29U
#define PVR_STREAM_EXTHDR_TYPE_MASK (7U << PVR_STREAM_EXTHDR_TYPE_SHIFT)
#define PVR_STREAM_EXTHDR_TYPE_MAX 8U
#define PVR_STREAM_EXTHDR_CONTINUATION BIT(28U)

#define PVR_STREAM_EXTHDR_DATA_MASK ~(PVR_STREAM_EXTHDR_TYPE_MASK | PVR_STREAM_EXTHDR_CONTINUATION)

/*
 * Stream extension header - Geometry 0
 */
#define PVR_STREAM_EXTHDR_TYPE_GEOM0 0U

#define PVR_STREAM_EXTHDR_GEOM0_BRN49927 BIT(0U)

#define PVR_STREAM_EXTHDR_GEOM0_VALID PVR_STREAM_EXTHDR_GEOM0_BRN49927

/*
 * Stream extension header - Fragment 0
 */
#define PVR_STREAM_EXTHDR_TYPE_FRAG0 0U

#define PVR_STREAM_EXTHDR_FRAG0_BRN47217 BIT(0U)
#define PVR_STREAM_EXTHDR_FRAG0_BRN49927 BIT(1U)

#define PVR_STREAM_EXTHDR_FRAG0_VALID PVR_STREAM_EXTHDR_FRAG0_BRN49927

/*
 * Stream extension header - Compute 0
 */
#define PVR_STREAM_EXTHDR_TYPE_COMPUTE0 0U

#define PVR_STREAM_EXTHDR_COMPUTE0_BRN49927 BIT(0U)

#define PVR_STREAM_EXTHDR_COMPUTE0_VALID PVR_STREAM_EXTHDR_COMPUTE0_BRN49927

#endif /* PVR_ROGUE_FWIF_STREAM_H */
