# SPDX-License-Identifier: (GPL-2.0+ OR MIT)

MPP_GIT_REVISION := \
	$(shell git log -1 --no-decorate --date=short \
	--pretty=format:"%h author: %<|(30)%an %cd %s" -- $(src) || \
	echo -n "unknown mpp version for missing VCS info")

MPP_REVISION_0 := $(subst \,\\\,$(MPP_GIT_REVISION))
MPP_REVISION   := $(subst ",\\\",$(MPP_REVISION_0))

rk_vcodec-objs := mpp_service.o mpp_common.o mpp_iommu.o
CFLAGS_mpp_service.o += -DMPP_VERSION="\"$(MPP_REVISION)\""

rk_vcodec-$(CONFIG_ROCKCHIP_MPP_RKVDEC) += mpp_rkvdec.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_RKVDEC2) += mpp_rkvdec2.o mpp_rkvdec2_link.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_RKVENC) += mpp_rkvenc.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_RKVENC2) += mpp_rkvenc2.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_VDPU1)  += mpp_vdpu1.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_VEPU1)  += mpp_vepu1.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_VDPU2)  += mpp_vdpu2.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_VEPU2)  += mpp_vepu2.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_IEP2)   += mpp_iep2.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_JPGDEC) += mpp_jpgdec.o
rk_vcodec-$(CONFIG_ROCKCHIP_MPP_AV1DEC) += mpp_av1dec.o mpp_iommu_av1d.o

# hack for workaround
rk_vcodec-$(CONFIG_CPU_PX30) += hack/mpp_hack_px30.o

obj-$(CONFIG_ROCKCHIP_MPP_SERVICE) += rk_vcodec.o
