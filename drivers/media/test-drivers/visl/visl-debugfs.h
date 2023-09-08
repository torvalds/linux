/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Debugfs tracing for bitstream buffers. This is similar to VA-API's
 * LIBVA_TRACE_BUFDATA in that the raw bitstream can be dumped as a debugging
 * aid.
 *
 * Produces one file per OUTPUT buffer. Files are automatically cleared on
 * STREAMOFF unless the module parameter "keep_bitstream_buffers" is set.
 */

#include "visl.h"
#include "visl-dec.h"

#ifdef CONFIG_VISL_DEBUGFS

int visl_debugfs_init(struct visl_dev *dev);
int visl_debugfs_bitstream_init(struct visl_dev *dev);
void visl_trace_bitstream(struct visl_ctx *ctx, struct visl_run *run);
void visl_debugfs_clear_bitstream(struct visl_dev *dev);
void visl_debugfs_bitstream_deinit(struct visl_dev *dev);
void visl_debugfs_deinit(struct visl_dev *dev);

#else

static inline int visl_debugfs_init(struct visl_dev *dev)
{
	return 0;
}

static inline int visl_debugfs_bitstream_init(struct visl_dev *dev)
{
	return 0;
}

static inline void visl_trace_bitstream(struct visl_ctx *ctx, struct visl_run *run) {}
static inline void visl_debugfs_clear_bitstream(struct visl_dev *dev) {}
static inline void visl_debugfs_bitstream_deinit(struct visl_dev *dev) {}
static inline void visl_debugfs_deinit(struct visl_dev *dev) {}

#endif
