#include <media/v4l2-common.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>

#define CREATE_TRACE_POINTS
#include <trace/events/v4l2.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(vb2_buf_done);
EXPORT_TRACEPOINT_SYMBOL_GPL(vb2_buf_queue);
EXPORT_TRACEPOINT_SYMBOL_GPL(vb2_dqbuf);
EXPORT_TRACEPOINT_SYMBOL_GPL(vb2_qbuf);
