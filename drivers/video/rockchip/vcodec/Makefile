rk-vcodec-objs := vcodec_service.o vcodec_iommu_ops.o

ifdef CONFIG_DRM
rk-vcodec-objs += vcodec_iommu_drm.o
endif

ifdef CONFIG_ION
rk-vcodec-objs += vcodec_iommu_ion.o
endif

obj-$(CONFIG_RK_VCODEC) += rk-vcodec.o
