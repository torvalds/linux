# SPDX-License-Identifier: GPL-2.0
ccflags-y += -include $(srctree)/drivers/staging/imgtec/config_kernel.h \
 -I$(srctree)/drivers/staging/imgtec \
 -I$(srctree)/$(src) \
 -I$(srctree)/$(src)/hwdefs \
 -I$(srctree)/$(src)/hwdefs/km \
 -I$(srctree)/$(src)/rk3368
ccflags-$(CONFIG_X86) += -mno-soft-float

ccflags-$(CONFIG_POWERVR_APOLLO:m=y) += -I$(srctree)/drivers/staging/imgtec/apollo
pvrsrvkm-$(CONFIG_POWERVR_APOLLO:m=y) += ../apollo/sysconfig.o

tmp := $(addprefix -I,$(wildcard $(srctree)/$(src)/generated/*))
ccflags-y += $(tmp)

obj-$(CONFIG_POWERVR_ROGUE_N) += pvrsrvkm.o

pvrsrvkm-y += rk3368/rk_init_v2.o
pvrsrvkm-y += rk3368/sysconfig.o
pvrsrvkm-y += interrupt_support.o
pvrsrvkm-y += pvr_dvfs_device.o

pvrsrvkm-y += allocmem.o
pvrsrvkm-y += cache_km.o
pvrsrvkm-y += connection_server.o
pvrsrvkm-y += debugmisc_server.o
pvrsrvkm-y += devicemem.o
pvrsrvkm-y += devicemem_heapcfg.o
pvrsrvkm-y += osmmap_stub.o
pvrsrvkm-y += devicemem_server.o
pvrsrvkm-y += devicemem_utils.o
pvrsrvkm-y += event.o
pvrsrvkm-y += handle.o
pvrsrvkm-y += handle_idr.o
pvrsrvkm-y += hash.o
pvrsrvkm-y += htbserver.o
pvrsrvkm-y += htbuffer.o
pvrsrvkm-y += km_apphint.o
pvrsrvkm-y += lists.o
pvrsrvkm-y += mem_utils.o
pvrsrvkm-y += mmu_common.o
pvrsrvkm-y += module_common.o
pvrsrvkm-y += osconnection_server.o
pvrsrvkm-y += osfunc.o
pvrsrvkm-y += physheap.o
pvrsrvkm-y += physmem.o
pvrsrvkm-y += physmem_lma.o
pvrsrvkm-y += physmem_osmem_linux.o
pvrsrvkm-y += physmem_tdsecbuf.o
pvrsrvkm-y += pmr.o
pvrsrvkm-y += pmr_os.o
pvrsrvkm-y += power.o
pvrsrvkm-y += process_stats.o
pvrsrvkm-y += pvr_bridge_k.o
pvrsrvkm-y += pvr_debug.o
pvrsrvkm-y += pvr_debugfs.o
pvrsrvkm-y += pvr_notifier.o
pvrsrvkm-y += pvrsrv.o
pvrsrvkm-y += ra.o
pvrsrvkm-y += rgx_compat_bvnc.o
pvrsrvkm-y += rgxbreakpoint.o
pvrsrvkm-y += rgxccb.o
pvrsrvkm-y += rgxcompute.o
pvrsrvkm-y += rgxdebug.o
pvrsrvkm-y += rgxfwutils.o
pvrsrvkm-y += rgxhwperf.o
pvrsrvkm-y += rgxinit.o
pvrsrvkm-y += rgxkicksync.o
pvrsrvkm-y += rgxlayer_km_impl.o
pvrsrvkm-y += rgxmem.o
pvrsrvkm-y += rgxmipsmmuinit.o
pvrsrvkm-y += rgxmmuinit.o
pvrsrvkm-y += rgxpower.o
pvrsrvkm-y += rgxray.o
pvrsrvkm-y += rgxregconfig.o
pvrsrvkm-y += rgxsignals.o
pvrsrvkm-y += rgxstartstop.o
pvrsrvkm-y += rgxta3d.o
pvrsrvkm-y += rgxtdmtransfer.o
pvrsrvkm-y += rgxtimecorr.o
pvrsrvkm-y += rgxtimerquery.o
pvrsrvkm-y += rgxtransfer.o
pvrsrvkm-y += rgxutils.o
pvrsrvkm-y += srvcore.o
pvrsrvkm-y += sync.o
pvrsrvkm-y += sync_checkpoint.o
pvrsrvkm-y += sync_server.o
pvrsrvkm-y += tlclient.o
pvrsrvkm-y += tlintern.o
pvrsrvkm-y += tlserver.o
pvrsrvkm-y += tlstream.o
pvrsrvkm-y += uniq_key_splay_tree.o

# Kernel srvinit
pvrsrvkm-y += rgx_compat_bvnc.o
pvrsrvkm-y += rgx_hwperf_table.o
pvrsrvkm-y += rgxfwimageutils.o
pvrsrvkm-y += rgxfwload.o
pvrsrvkm-y += rgxlayer_impl.o
pvrsrvkm-y += rgxsrvinit.o
pvrsrvkm-y += rgxsrvinit_script.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/dpdump_bridge/client_pdump_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/dpdumpctrl_bridge/client_pdumpctrl_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/drgxpdump_bridge/client_rgxpdump_bridge.o
pvrsrvkm-y += generated/rgxinit_bridge/client_rgxinit_direct_bridge.o

pvrsrvkm-y += ../pvr_platform_drv.o

pvrsrvkm-$(CONFIG_DRM) += ../pvr_drm.o

pvrsrvkm-$(CONFIG_DMA_SHARED_BUFFER) += physmem_dmabuf.o

# RI strings
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_RESOURCE_INFO) += ri_server.o
ccflags-$(CONFIG_POWERVR_ROGUE_RESOURCE_INFO) += -DPVR_RI_DEBUG=1

# Device memory history
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_DEVICEMEM_HISTORY) += devicemem_history_server.o
ccflags-$(CONFIG_POWERVR_ROGUE_DEVICEMEM_HISTORY) += -DSUPPORT_PAGE_FAULT_DEBUG=1

# GPU tracing in systrace
#pvrsrvkm-$(CONFIG_FTRACE) += pvr_gputrace.o

# Event tracing
pvrsrvkm-$(CONFIG_EVENT_TRACING) += trace_events.o

# arch-specific wrapper functions
pvrsrvkm-$(CONFIG_X86) += osfunc_x86.o
pvrsrvkm-$(CONFIG_ARM) += osfunc_arm.o
pvrsrvkm-$(CONFIG_ARM64) += osfunc_arm64.o
pvrsrvkm-$(CONFIG_METAG) += osfunc_metag.o
pvrsrvkm-$(CONFIG_MIPS) += osfunc_mips.o

# PDump
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += dbgdriv.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += dbgdriv_handle.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += devicemem_pdump.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += devicememx_pdump.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += hostfunc.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += ioctl.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += main.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += pdump.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += pdump_common.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += pdump_mmu.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += pdump_physmem.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += rgxpdump.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += srvinit_pdump.o
ccflags-$(CONFIG_POWERVR_ROGUE_PDUMP) += -DPDUMP=1

# Android native synchronisation
pvrsrvkm-$(CONFIG_SYNC) += ../pvr_sync.o

# Generated bridge code
pvrsrvkm-y += generated/mm_bridge/server_mm_bridge.o
pvrsrvkm-y += generated/mm_bridge/client_mm_direct_bridge.o
pvrsrvkm-y += generated/cmm_bridge/server_cmm_bridge.o
pvrsrvkm-y += generated/rgxtq_bridge/server_rgxtq_bridge.o
pvrsrvkm-y += generated/rgxta3d_bridge/server_rgxta3d_bridge.o
pvrsrvkm-y += generated/rgxcmp_bridge/server_rgxcmp_bridge.o
pvrsrvkm-y += generated/rgxsignals_bridge/server_rgxsignals_bridge.o
pvrsrvkm-y += generated/srvcore_bridge/server_srvcore_bridge.o
pvrsrvkm-y += generated/sync_bridge/server_sync_bridge.o
pvrsrvkm-y += generated/sync_bridge/client_sync_direct_bridge.o
pvrsrvkm-y += generated/cache_bridge/client_cache_direct_bridge.o
pvrsrvkm-y += generated/cache_bridge/server_cache_bridge.o
pvrsrvkm-y += generated/breakpoint_bridge/server_breakpoint_bridge.o
pvrsrvkm-y += generated/debugmisc_bridge/server_debugmisc_bridge.o
pvrsrvkm-y += generated/pvrtl_bridge/server_pvrtl_bridge.o
pvrsrvkm-y += generated/pvrtl_bridge/client_pvrtl_direct_bridge.o
pvrsrvkm-y += generated/rgxhwperf_bridge/server_rgxhwperf_bridge.o
pvrsrvkm-y += generated/regconfig_bridge/server_regconfig_bridge.o
pvrsrvkm-y += generated/timerquery_bridge/server_timerquery_bridge.o
pvrsrvkm-y += generated/htbuffer_bridge/server_htbuffer_bridge.o
pvrsrvkm-y += generated/htbuffer_bridge/client_htbuffer_direct_bridge.o
pvrsrvkm-y += generated/rgxkicksync_bridge/server_rgxkicksync_bridge.o
pvrsrvkm-y += generated/rgxray_bridge/server_rgxray_bridge.o
pvrsrvkm-y += generated/rgxtq2_bridge/server_rgxtq2_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_RESOURCE_INFO) += generated/ri_bridge/server_ri_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_RESOURCE_INFO) += generated/ri_bridge/client_ri_direct_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_DEVICEMEM_HISTORY) += generated/devicememhistory_bridge/server_devicememhistory_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_DEVICEMEM_HISTORY) += generated/devicememhistory_bridge/client_devicememhistory_direct_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/pdumpmm_bridge/server_pdumpmm_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/pdumpmm_bridge/client_pdumpmm_direct_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/pdump_bridge/server_pdump_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/pdumpctrl_bridge/client_pdumpctrl_direct_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/pdumpctrl_bridge/server_pdumpctrl_bridge.o
pvrsrvkm-$(CONFIG_POWERVR_ROGUE_PDUMP) += generated/rgxpdump_bridge/server_rgxpdump_bridge.o
pvrsrvkm-$(CONFIG_DMA_SHARED_BUFFER) += generated/dmabuf_bridge/server_dmabuf_bridge.o

