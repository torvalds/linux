/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VIRTGPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VIRTGPU_TRACE_H_

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM virtio_gpu
#define TRACE_INCLUDE_FILE virtgpu_trace

DECLARE_EVENT_CLASS(virtio_gpu_cmd,
	TP_PROTO(struct virtqueue *vq, struct virtio_gpu_ctrl_hdr *hdr, u32 seqno),
	TP_ARGS(vq, hdr, seqno),
	TP_STRUCT__entry(
			 __field(int, dev)
			 __field(unsigned int, vq)
			 __string(name, vq->name)
			 __field(u32, type)
			 __field(u32, flags)
			 __field(u64, fence_id)
			 __field(u32, ctx_id)
			 __field(u32, num_free)
			 __field(u32, seqno)
			 ),
	TP_fast_assign(
		       __entry->dev = vq->vdev->index;
		       __entry->vq = vq->index;
		       __assign_str(name, vq->name);
		       __entry->type = le32_to_cpu(hdr->type);
		       __entry->flags = le32_to_cpu(hdr->flags);
		       __entry->fence_id = le64_to_cpu(hdr->fence_id);
		       __entry->ctx_id = le32_to_cpu(hdr->ctx_id);
		       __entry->num_free = vq->num_free;
		       __entry->seqno = seqno;
		       ),
	TP_printk("vdev=%d vq=%u name=%s type=0x%x flags=0x%x fence_id=%llu ctx_id=%u num_free=%u seqno=%u",
		  __entry->dev, __entry->vq, __get_str(name),
		  __entry->type, __entry->flags, __entry->fence_id,
		  __entry->ctx_id, __entry->num_free, __entry->seqno)
);

DEFINE_EVENT(virtio_gpu_cmd, virtio_gpu_cmd_queue,
	TP_PROTO(struct virtqueue *vq, struct virtio_gpu_ctrl_hdr *hdr, u32 seqno),
	TP_ARGS(vq, hdr, seqno)
);

DEFINE_EVENT(virtio_gpu_cmd, virtio_gpu_cmd_response,
	TP_PROTO(struct virtqueue *vq, struct virtio_gpu_ctrl_hdr *hdr, u32 seqno),
	TP_ARGS(vq, hdr, seqno)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/virtio
#include <trace/define_trace.h>
