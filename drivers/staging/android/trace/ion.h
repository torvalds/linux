#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../drivers/staging/android/trace
#define TRACE_SYSTEM ion

#if !defined(_TRACE_ION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ION_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(ion_buffer_op,
	TP_PROTO(const char* client, void* buf, unsigned int size),
	TP_ARGS(client, buf, size),
	TP_STRUCT__entry(
		__string(client, client)
		__field(void*, buf)
		__field(unsigned int, size)
	),
	TP_fast_assign(
		__assign_str(client, client);
		__entry->buf = buf;
		__entry->size = size;
	),
	TP_printk("client=%s,buffer=%p:%d",
		  __get_str(client), __entry->buf, __entry->size)
);
DEFINE_EVENT(ion_buffer_op, ion_buffer_alloc,
	TP_PROTO(const char* client, void* buffer, unsigned int size),
	TP_ARGS(client, buffer, size));

DEFINE_EVENT(ion_buffer_op, ion_buffer_free,
	TP_PROTO(const char* client, void* buffer, unsigned int size),
	TP_ARGS(client, buffer, size));

DEFINE_EVENT(ion_buffer_op, ion_buffer_import,
	TP_PROTO(const char* client, void* buffer, unsigned int size),
	TP_ARGS(client, buffer, size));

DEFINE_EVENT(ion_buffer_op, ion_buffer_destroy,
	TP_PROTO(const char* client, void* buffer, unsigned int size),
	TP_ARGS(client, buffer, size));

DEFINE_EVENT(ion_buffer_op, ion_kernel_unmap,
	TP_PROTO(const char* client, void* buffer, unsigned int size),
	TP_ARGS(client, buffer, size));

TRACE_EVENT(ion_buffer_share,
	TP_PROTO(const char* client, void* buf, unsigned int size, int fd),
	TP_ARGS(client, buf, size, fd),
	TP_STRUCT__entry(
		__string(client, client)
		__field(void*, buf)
		__field(unsigned int, size)
		__field(int, fd)
	),
	TP_fast_assign(
		__assign_str(client, client);
		__entry->buf = buf;
		__entry->size = size;
		__entry->fd = fd;
	),
	TP_printk("client=%s,buffer=%p:%d,fd=%d",
		  __get_str(client), __entry->buf, __entry->size, __entry->fd)
);

DECLARE_EVENT_CLASS(ion_client_op,
	TP_PROTO(const char* client),
	TP_ARGS(client),
	TP_STRUCT__entry(
		__string(client, client)
	),
	TP_fast_assign(
		__assign_str(client, client);
	),
	TP_printk("client=%s", __get_str(client))
);
DEFINE_EVENT(ion_client_op, ion_client_create,
	TP_PROTO(const char* client),
	TP_ARGS(client));
DEFINE_EVENT(ion_client_op, ion_client_destroy,
	TP_PROTO(const char* client),
	TP_ARGS(client));

DECLARE_EVENT_CLASS(ion_iommu_op,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		const char* iommu_dev, unsigned int iommu_addr,
		unsigned int iommu_size, unsigned int map_cnt),
	TP_ARGS(client, buf, size, iommu_dev, iommu_addr, iommu_size, map_cnt),
	TP_STRUCT__entry(
		__string(client, client)
		__field(void*, buf)
		__field(unsigned int, size)
		__string(iommu_dev, iommu_dev)
		__field(unsigned int, iommu_addr)
		__field(unsigned int, iommu_size)
		__field(unsigned int, map_cnt)
	),
	TP_fast_assign(
		__assign_str(client, client);
		__entry->buf = buf;
		__entry->size = size;
		__assign_str(iommu_dev, iommu_dev);
		__entry->iommu_addr = iommu_addr;
		__entry->iommu_size = iommu_size;
		__entry->map_cnt = map_cnt;
	),
	TP_printk("client=%s,buffer=%p:%d,iommu=%s,map=%08x:%d,map_count=%d",
		  __get_str(client), __entry->buf, __entry->size,
		  __get_str(iommu_dev), __entry->iommu_addr, __entry->iommu_size,
		  __entry->map_cnt)
);
DEFINE_EVENT(ion_iommu_op, ion_iommu_map,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		const char* iommu_dev, unsigned int iommu_addr,
		unsigned int iommu_size, unsigned int map_cnt),
	TP_ARGS(client, buf, size, iommu_dev, iommu_addr, iommu_size, map_cnt));
DEFINE_EVENT(ion_iommu_op, ion_iommu_unmap,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		const char* iommu_dev, unsigned int iommu_addr,
		unsigned int iommu_size, unsigned int map_cnt),
	TP_ARGS(client, buf, size, iommu_dev, iommu_addr, iommu_size, map_cnt));
DEFINE_EVENT(ion_iommu_op, ion_iommu_release,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		const char* iommu_dev, unsigned int iommu_addr,
		unsigned int iommu_size, unsigned int map_cnt),
	TP_ARGS(client, buf, size, iommu_dev, iommu_addr, iommu_size, map_cnt));

DECLARE_EVENT_CLASS(ion_kmap_op,
	TP_PROTO(const char* client, void* buf, unsigned int size, void* kaddr),
	TP_ARGS(client, buf, size, kaddr),
	TP_STRUCT__entry(
		__string(client, client)
		__field(void*, buf)
		__field(unsigned int, size)
		__field(void*, kaddr)
	),
	TP_fast_assign(
		__assign_str(client, client);
		__entry->buf = buf;
		__entry->size = size;
		__entry->kaddr = kaddr;
	),
	TP_printk("client=%s,buffer=%p:%d,kaddr=%p",
		  __get_str(client), __entry->buf, __entry->size, __entry->kaddr)
);
DEFINE_EVENT(ion_kmap_op, ion_kernel_map,
	TP_PROTO(const char* client, void* buffer, unsigned int size, void* kaddr),
	TP_ARGS(client, buffer, size, kaddr));

DECLARE_EVENT_CLASS(ion_mmap_op,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		unsigned long vm_start, unsigned long vm_end),
	TP_ARGS(client, buf, size, vm_start, vm_end),
	TP_STRUCT__entry(
		__string(client, client)
		__field(void*, buf)
		__field(unsigned int, size)
		__field(unsigned long, vm_start)
		__field(unsigned long, vm_end)
	),
	TP_fast_assign(
		__assign_str(client, client);
		__entry->buf = buf;
		__entry->size = size;
		__entry->vm_start = vm_start;
		__entry->vm_end = vm_end;
	),
	TP_printk("client=%s,buffer=%p:%d,vma[%08lx:%08lx]",
		  __get_str(client), __entry->buf, __entry->size,
		  __entry->vm_start, __entry->vm_end)
);

DEFINE_EVENT(ion_mmap_op, ion_buffer_mmap,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		unsigned long vm_start, unsigned long vm_end),
	TP_ARGS(client, buf, size, vm_start, vm_end));

DEFINE_EVENT(ion_mmap_op, ion_buffer_munmap,
	TP_PROTO(const char* client, void* buf, unsigned int size,
		unsigned long vm_start, unsigned long vm_end),
	TP_ARGS(client, buf, size, vm_start, vm_end));

#endif /* _TRACE_ION_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
