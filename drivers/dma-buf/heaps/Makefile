# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DMABUF_HEAPS_DEFERRED_FREE) += deferred-free-helper.o
obj-$(CONFIG_DMABUF_HEAPS_PAGE_POOL)	+= page_pool.o
obj-$(CONFIG_DMABUF_HEAPS_SYSTEM)	+= system_heap.o
obj-$(CONFIG_DMABUF_HEAPS_CMA)		+= cma_heap.o
obj-$(CONFIG_QCOM_DMABUF_HEAPS)		+= qcom_dma_heaps.o
qcom_dma_heaps-y			:= qcom_dma_heap.o qcom_dt_parser.o qcom_sg_ops.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_SYSTEM) += qcom_system_heap.o \
						     qcom_dynamic_page_pool.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_SYSTEM_SECURE) += qcom_secure_system_heap.o \
							    qcom_dynamic_page_pool.o \
							    qcom_dma_heap_secure_utils.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_CMA)	+= qcom_cma_heap.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_CARVEOUT) += qcom_carveout_heap.o \
						       qcom_dma_heap_secure_utils.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_BITSTREAM_CONTIG) += qcom_bitstream_contig_heap.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_UBWCP) += qcom_ubwcp_heap.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_TUI_CARVEOUT) += qcom_tui_carveout_heap.o
qcom_dma_heaps-$(CONFIG_QCOM_DMABUF_HEAPS_SYSTEM_MOVABLE) += qcom_system_movable_heap.o
