include drivers/gpu/rogue/config_kernel.mk

obj-$(CONFIG_POWERVR_ROGUE) += pvrsrvkm.o

ccflags-y += -include drivers/gpu/rogue/config_kernel.h \
 -Idrivers/gpu/rogue \
 -Idrivers/gpu/rogue/hwdefs \
 -Idrivers/gpu/rogue/hwdefs/km \
 -Idrivers/gpu/rogue/system/include \
 -Idrivers/gpu/rogue/system/common \
 -Idrivers/gpu/rogue/system/$(PVR_SYSTEM) \
 -Idrivers/gpu/rogue/services/server/include \
 -Idrivers/gpu/rogue/include \
 -Idrivers/gpu/rogue/services/include \
 -Idrivers/gpu/rogue/services/include/shared \
 -Idrivers/gpu/rogue/services/shared/include \
 -Idrivers/gpu/rogue/services/shared/devices/rgx \
 -Idrivers/gpu/rogue/services/server/env/linux \
 -Idrivers/gpu/rogue/kernel/drivers/staging/imgtec \
 -Idrivers/gpu/rogue/services/include/env/linux \
 -Idrivers/gpu/rogue/services/server/devices/rgx

tmp := $(addprefix -I,$(wildcard drivers/gpu/rogue/generated/*))
ccflags-y += $(tmp)

########
pvrsrvkm-y += \
 services/server/env/linux/event.o \
 services/server/env/linux/mm.o \
 services/server/env/linux/mmap.o \
 services/server/env/linux/module.o \
 services/server/env/linux/devicemem_mmap_stub.o \
 services/server/env/linux/osfunc.o \
 services/server/env/linux/allocmem.o \
 services/server/env/linux/osconnection_server.o \
 services/server/env/linux/pdump.o \
 services/server/env/linux/physmem_osmem_linux.o \
 services/server/env/linux/pvr_debugfs.o \
 services/server/env/linux/pvr_bridge_k.o \
 services/server/env/linux/pvr_debug.o \
 services/server/env/linux/physmem_tdmetacode_linux.o \
 services/server/env/linux/physmem_tdsecbuf_linux.o \
 services/server/common/devicemem_heapcfg.o \
 services/shared/common/devicemem.o \
 services/shared/common/devicemem_utils.o \
 services/shared/common/hash.o \
 services/shared/common/ra.o \
 services/shared/common/sync.o \
 services/shared/common/dllist.o \
 services/server/common/devicemem_server.o \
 services/server/common/handle.o \
 services/server/common/lists.o \
 services/server/common/mmu_common.o \
 services/server/common/connection_server.o \
 services/server/common/physheap.o \
 services/server/common/physmem.o \
 services/server/common/physmem_lma.o \
 services/server/common/pmr.o \
 services/server/common/power.o \
 services/server/common/process_stats.o \
 services/server/common/pvrsrv.o \
 services/server/common/resman.o \
 services/server/common/srvcore.o \
 services/server/common/sync_server.o \
 services/server/common/tlintern.o \
 services/shared/common/tlclient.o \
 services/server/common/tlserver.o \
 services/server/common/tlstream.o \
 services/server/common/tutils.o \
 services/shared/common/uniq_key_splay_tree.o \
 services/server/devices/rgx/rgxsync.o

ifeq ($(SUPPORT_DISPLAY_CLASS),1)
pvrsrvkm-y += \
 services/server/common/dc_server.o \
 services/server/common/scp.o
endif

ifeq ($(PVR_RI_DEBUG),1)
pvrsrvkm-y += services/server/common/ri_server.o
endif

ifeq ($(PVR_HANDLE_BACKEND),generic)
pvrsrvkm-y += services/server/common/handle_generic.o
else
ifeq ($(PVR_HANDLE_BACKEND),idr)
pvrsrvkm-y += handle_idr.o
endif
endif

ifeq ($(SUPPORT_GPUTRACE_EVENTS),1)
pvrsrvkm-y += services/server/env/linux/pvr_gputrace.o
endif


pvrsrvkm-$(CONFIG_X86) += services/server/env/linux/osfunc_x86.o
pvrsrvkm-$(CONFIG_ARM) += services/server/env/linux/osfunc_arm.o
pvrsrvkm-$(CONFIG_ARM64) += services/server/env/linux/osfunc_arm64.o
pvrsrvkm-$(CONFIG_METAG) += osfunc_metag.o
pvrsrvkm-$(CONFIG_MIPS) += osfunc_mips.o
pvrsrvkm-$(CONFIG_EVENT_TRACING) += services/server/env/linux/trace_events.o


ifeq ($(PDUMP),1)
pvrsrvkm-y += \
 services/server/common/pdump_common.o \
 services/server/common/pdump_mmu.o \
 services/server/common/pdump_physmem.o \
 services/shared/common/devicemem_pdump.o
endif

pvrsrvkm-y += \
 services/server/devices/rgx/rgxinit.o \
 services/server/devices/rgx/rgxdebug.o \
 services/server/devices/rgx/rgxhwperf.o \
 services/server/devices/rgx/rgxmem.o \
 services/server/devices/rgx/rgxta3d.o \
 services/server/devices/rgx/rgxcompute.o \
 services/server/devices/rgx/rgxccb.o \
 services/server/devices/rgx/rgxmmuinit.o \
 services/server/devices/rgx/rgxpower.o \
 services/server/devices/rgx/rgxtransfer.o \
 services/server/devices/rgx/rgxutils.o \
 services/server/devices/rgx/rgxfwutils.o \
 services/server/devices/rgx/rgxbreakpoint.o \
 services/server/devices/rgx/debugmisc_server.o \
 services/shared/devices/rgx/rgx_compat_bvnc.o \
 services/server/devices/rgx/rgxregconfig.o \
 services/server/devices/rgx/rgxtimerquery.o

ifeq ($(SUPPORT_RAY_TRACING),1)
pvrsrvkm-y += rgxray.o
endif

ifeq ($(PDUMP),1)
pvrsrvkm-y += services/server/devices/rgx/rgxpdump.o
endif

ifeq ($(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC),1)
pvrsrvkm-y += \
 kernel/drivers/staging/imgtec/pvr_sync.o
endif

pvrsrvkm-y += \
 generated/mm_bridge/server_mm_bridge.o \
 generated/dmm_bridge/client_mm_bridge.o \
 generated/pdumpmm_bridge/server_pdumpmm_bridge.o \
 generated/dpdumpmm_bridge/client_pdumpmm_bridge.o \
 generated/cmm_bridge/server_cmm_bridge.o \
 generated/pdumpcmm_bridge/server_pdumpcmm_bridge.o \
 generated/pdump_bridge/server_pdump_bridge.o \
 generated/rgxtq_bridge/server_rgxtq_bridge.o \
 generated/rgxinit_bridge/server_rgxinit_bridge.o \
 generated/rgxta3d_bridge/server_rgxta3d_bridge.o \
 generated/rgxcmp_bridge/server_rgxcmp_bridge.o \
 generated/srvcore_bridge/server_srvcore_bridge.o \
 generated/sync_bridge/server_sync_bridge.o \
 generated/dsync_bridge/client_sync_bridge.o \
 generated/breakpoint_bridge/server_breakpoint_bridge.o \
 generated/debugmisc_bridge/server_debugmisc_bridge.o \
 generated/rgxpdump_bridge/server_rgxpdump_bridge.o \
 generated/pvrtl_bridge/server_pvrtl_bridge.o \
 generated/dpvrtl_bridge/client_pvrtl_bridge.o \
 generated/rgxhwperf_bridge/server_rgxhwperf_bridge.o \
 generated/regconfig_bridge/server_regconfig_bridge.o \
 generated/timerquery_bridge/server_timerquery_bridge.o

ifeq ($(PVR_RI_DEBUG),1)
pvrsrvkm-y += \
 generated/ri_bridge/server_ri_bridge.o \
 generated/dri_bridge/client_ri_bridge.o
endif

ifeq ($(SUPPORT_DISPLAY_CLASS),1)
pvrsrvkm-y += \
 generated/dc_bridge/server_dc_bridge.o
endif

ifeq ($(SUPPORT_RAY_TRACING),1)
pvrsrvkm-y += generated/rgxray_bridge/server_rgxray_bridge.o
endif

ifeq ($(CACHEFLUSH_TYPE),CACHEFLUSH_GENERIC)
pvrsrvkm-y += \
 services/server/common/cache_generic.o \
 generated/cachegeneric_bridge/server_cachegeneric_bridge.o
endif

ifeq ($(SUPPORT_SECURE_EXPORT),1)
pvrsrvkm-y += \
 services/server/env/linux/ossecure_export.o \
 generated/smm_bridge/server_smm_bridge.o \
 generated/syncsexport_bridge/server_syncsexport_bridge.o
endif
ifeq ($(SUPPORT_INSECURE_EXPORT),1)
pvrsrvkm-y += \
 generated/syncexport_bridge/server_syncexport_bridge.o
endif

ifeq ($(SUPPORT_ION),1)
pvrsrvkm-y += generated/dmabuf_bridge/server_dmabuf_bridge.o
pvrsrvkm-y += services/server/env/linux/physmem_dmabuf.o
endif # SUPPORT_ION

#ccflags-y += \
# -mno-soft-float

#dc_pdp-y += \
#	dc_pdp/dc_pdp.o	\
#	dc_pdp/pdp_osfuncs.o	\
#	dc_osfuncs.o

pvrsrvkm-y += \
	system/$(PVR_SYSTEM)/sysconfig.o
#	system/common/pci_support.o

ifeq ($(PVR_SYSTEM),rgx_tc)
pvrsrvkm-y += system/$(PVR_SYSTEM)/apollo_flasher_linux.o
endif

ifeq ($(PVR_SYSTEM),rk3368)
pvrsrvkm-y += system/$(PVR_SYSTEM)/rk_init.o
endif

#ifneq ($(SUPPORT_DRM),1)
#pvrsrvkm-y += system/common/interrupt_support.o
#endif

ifeq ($(SUPPORT_ION),1)
ifeq ($(LMA),1)
# For LMA, use the TC-specific ion heap.
pvrsrvkm-y += \
	system/$(PVR_SYSTEM)/ion_support.o \
	system/common/ion_lma_heap.o
else
# For UMA, use the generic ion support code, which creates heaps from system
# memory.
#pvrsrvkm-y += \
#	system/common/ion_support_generic.o
endif # LMA
endif # SUPPORT_ION

dbgdrv-y += \
	tools/services/debug/dbgdriv/common/dbgdriv.o \
	tools/services/debug/dbgdriv/common/ioctl.o \
	tools/services/debug/dbgdriv/common/handle.o \
	tools/services/debug/dbgdriv/linux/main.o \
	tools/services/debug/dbgdriv/linux/hostfunc.o

