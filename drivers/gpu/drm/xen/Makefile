# SPDX-License-Identifier: GPL-2.0 OR MIT

drm_xen_front-objs := xen_drm_front.o \
		      xen_drm_front_kms.o \
		      xen_drm_front_conn.o \
		      xen_drm_front_evtchnl.o \
		      xen_drm_front_shbuf.o \
		      xen_drm_front_cfg.o

ifeq ($(CONFIG_DRM_XEN_FRONTEND_CMA),y)
	drm_xen_front-objs += xen_drm_front_gem_cma.o
else
	drm_xen_front-objs += xen_drm_front_gem.o
endif

obj-$(CONFIG_DRM_XEN_FRONTEND) += drm_xen_front.o
