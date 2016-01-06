########################################################################### ###
#@File
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

pvrsrvkm-y += \
 services/server/env/linux/event.o \
 services/server/env/linux/mm.o \
 services/server/env/linux/mmap.o \
 services/server/env/linux/module_common.o \
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
 services/shared/common/mem_utils.o \
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
 services/server/common/srvcore.o \
 services/server/common/sync_server.o \
 services/server/common/tlintern.o \
 services/shared/common/tlclient.o \
 services/server/common/tlserver.o \
 services/server/common/tlstream.o \
 services/shared/common/uniq_key_splay_tree.o

ifneq ($(PVR_LOADER),)
pvrsrvkm-y += services/server/env/linux/$(PVR_LOADER).o
else
pvrsrvkm-y += services/server/env/linux/module.o
endif

ifeq ($(SUPPORT_DISPLAY_CLASS),1)
pvrsrvkm-y += \
 services/server/common/dc_server.o \
 services/server/common/scp.o
endif

ifeq ($(PVR_RI_DEBUG),1)
pvrsrvkm-y += services/server/common/ri_server.o
endif

ifeq ($(SUPPORT_PAGE_FAULT_DEBUG),1)
pvrsrvkm-y += services/server/common/devicemem_history_server.o
endif

ifeq ($(PVR_HANDLE_BACKEND),generic)
pvrsrvkm-y += services/server/common/handle_generic.o
else
ifeq ($(PVR_HANDLE_BACKEND),idr)
pvrsrvkm-y += services/server/env/linux/handle_idr.o
endif
endif

ifeq ($(SUPPORT_GPUTRACE_EVENTS),1)
pvrsrvkm-y += services/server/env/linux/pvr_gputrace.o
endif

ifeq ($(PVR_TESTING_UTILS),1)
pvrsrvkm-y += services/server/common/tutils.o
endif

ifeq ($(PVR_DVFS),1)
pvrsrvkm-y += \
 services/server/env/linux/pvr_dvfs_governor.o \
 services/server/env/linux/pvr_dvfs_device.o
endif

pvrsrvkm-$(CONFIG_X86) += services/server/env/linux/osfunc_x86.o
pvrsrvkm-$(CONFIG_ARM) += services/server/env/linux/osfunc_arm.o
pvrsrvkm-$(CONFIG_ARM64) += services/server/env/linux/osfunc_arm64.o
pvrsrvkm-$(CONFIG_METAG) += services/server/env/linux/osfunc_metag.o
pvrsrvkm-$(CONFIG_MIPS) += services/server/env/linux/osfunc_mips.o

pvrsrvkm-$(CONFIG_EVENT_TRACING) += services/server/env/linux/trace_events.o

ifneq ($(W),1)
CFLAGS_mm.o := -Werror
CFLAGS_mmap.o := -Werror
CFLAGS_module_common.o := -Werror
CFLAGS_mutils.o := -Werror
CFLAGS_devicemem_mmap_stub.o := -Werror
CFLAGS_osfunc.o := -Werror
CFLAGS_osfunc_x86.o := -Werror
CFLAGS_osconnection_server.o := -Werror
CFLAGS_pdump.o := -Werror
CFLAGS_pvr_debugfs.o := -Werror
CFLAGS_pvr_bridge_k.o := -Werror
CFLAGS_devicemem_heapcfg.o := -Werror
CFLAGS_devicemem.o := -Werror
CFLAGS_devicemem_utils.o = -Werror
CFLAGS_devicemem_pdump.o = -Werror
CFLAGS_hash.o := -Werror
CFLAGS_ra.o := -Werror
CFLAGS_sync.o := -Werror
CFLAGS_devicemem_server.o := -Werror
CFLAGS_handle.o := -Werror
CFLAGS_lists.o := -Werror
CFLAGS_mem_debug.o := -Werror
CFLAGS_mmu_common.o := -Werror
CFLAGS_connection_server.o := -Werror
CFLAGS_physmem.o := -Werror
CFLAGS_physmem_lma.o := -Werror
CFLAGS_pmr.o := -Werror
CFLAGS_power.o := -Werror
CFLAGS_process_stats.o := -Werror
CFLAGS_pvrsrv.o := -Werror
CFLAGS_srvcore.o := -Werror
CFLAGS_sync_server.o := -Werror
CFLAGS_tlintern.o := -Werror
CFLAGS_tlclient.o := -Werror
CFLAGS_tlserver.o := -Werror
CFLAGS_tlstream.o := -Werror

ifeq ($(SUPPORT_DISPLAY_CLASS),1)
CFLAGS_dc_server.o := -Werror
CFLAGS_scp.o := -Werror
endif

ifeq ($(PVR_RI_DEBUG),1)
CFLAGS_ri_server.o := -Werror
endif
ifeq ($(SUPPORT_PAGE_FAULT_DEBUG),1)
CFLAGS_devicememhistory_server.o := -Werror
endif
ifeq ($(SUPPORT_GPUTRACE_EVENTS),1)
CFLAGS_pvr_gputrace.o := -Werror
endif
ifeq ($(SUPPORT_VALIDATION),1)
CFLAGS_validation.o := -Werror
endif
ifeq ($(PVR_TESTING_UTILS),1)
CFLAGS_tutils.o := -Werror
endif
endif

# With certain build configurations, e.g., ARM, Werror, we get a build 
# failure in the ftrace Linux kernel header.  So disable the relevant check.
CFLAGS_trace_events.o := -Wno-missing-prototypes

ifeq ($(PDUMP),1)
pvrsrvkm-y += \
 services/server/common/pdump_common.o \
 services/server/common/pdump_mmu.o \
 services/server/common/pdump_physmem.o \
 services/shared/common/devicemem_pdump.o
ifneq ($(W),1)
CFLAGS_pdump_common.o := -Werror
CFLAGS_pdump_mmu.o := -Werror
CFLAGS_pdump_physmem.o := -Werror
CFLAGS_devicemem_pdump.o := -Werror
endif
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
 services/server/devices/rgx/rgxtimerquery.o \
 services/server/devices/rgx/rgxsync.o \
 services/server/devices/rgx/rgxtimecorr.o

ifeq ($(SUPPORT_RAY_TRACING),1)
pvrsrvkm-y += services/server/devices/rgx/rgxray.o
endif


ifneq ($(W),1)
CFLAGS_rgxinit.o := -Werror
CFLAGS_rgxdebug.o := -Werror
CFLAGS_rgxhwperf.o := -Werror
CFLAGS_rgxmem.o := -Werror
CFLAGS_rgxta3d.o := -Werror
CFLAGS_rgxcompute.o := -Werror
CFLAGS_rgxccb.o := -Werror
CFLAGS_rgxmmuinit.o := -Werror
CFLAGS_rgxpower.o := -Werror
CFLAGS_rgxsharedpb.o := -Werror
CFLAGS_rgxtransfer.o := -Werror
CFLAGS_rgxutils.o := -Werror
CFLAGS_rgxfwutils.o := -Werror
CFLAGS_rgxbreakpoint.o := -Werror
CFLAGS_debugmisc_server.o := -Werror
CFLAGS_rgxray.o := -Werror
CFLAGS_rgxregconfig.o := -Werror
CFLAGS_rgxsync.o := -Werror
endif

ifeq ($(PDUMP),1)
pvrsrvkm-y += services/server/devices/rgx/rgxpdump.o
ifneq ($(W),1)
CFLAGS_rgxpdump.o := -Werror
endif
endif

# Bridge headers and source files
ccflags-y += \
 -I$(bridge_base)/dmm_bridge \
 -I$(bridge_base)/mm_bridge \
 -I$(bridge_base)/cmm_bridge \
 -I$(bridge_base)/pdumpmm_bridge \
 -I$(bridge_base)/pdump_bridge \
 -I$(bridge_base)/pdumpctrl_bridge \
 -I$(bridge_base)/rgxtq_bridge \
 -I$(bridge_base)/rgxinit_bridge \
 -I$(bridge_base)/rgxta3d_bridge \
 -I$(bridge_base)/rgxcmp_bridge \
 -I$(bridge_base)/dsync_bridge \
 -I$(bridge_base)/sync_bridge \
 -I$(bridge_base)/breakpoint_bridge \
 -I$(bridge_base)/debugmisc_bridge \
 -I$(bridge_base)/rgxpdump_bridge \
 -I$(bridge_base)/pvrtl_bridge \
 -I$(bridge_base)/dpvrtl_bridge \
 -I$(bridge_base)/rgxhwperf_bridge \
 -I$(bridge_base)/regconfig_bridge \
 -I$(bridge_base)/timerquery_bridge

ifeq ($(SUPPORT_KERNEL_SRVINIT),1)
ccflags-y += \
 -I$(bridge_base)/dsrvcore_bridge \
 -I$(bridge_base)/drgxinit_bridge \
 -I$(bridge_base)/dpdumpctrl_bridge \
 -I$(bridge_base)/dpdump_bridge \
 -I$(bridge_base)/drgxpdump_bridge
endif

ccflags-y += \
 -I$(bridge_base)/srvcore_bridge

ifeq ($(PVR_RI_DEBUG),1)
ccflags-y += \
 -I$(bridge_base)/ri_bridge \
 -I$(bridge_base)/dri_bridge
endif

ifeq ($(SUPPORT_PAGE_FAULT_DEBUG),1)
ccflags-y += \
 -I$(bridge_base)/devicememhistory_bridge \
 -I$(bridge_base)/ddevicememhistory_bridge
endif


ifeq ($(SUPPORT_RAY_TRACING),1)
ccflags-y += -I$(bridge_base)/rgxray_bridge
endif

# Compatibility BVNC
ccflags-y += \
 -I$(TOP)/services/shared/devices/rgx

# Errata files
ccflags-y += \
 -I$(TOP)/hwdefs

ccflags-y += -I$(TOP)/services/include/env/linux

ifeq ($(SUPPORT_DISPLAY_CLASS),1)
ccflags-y += \
 -I$(bridge_base)/dc_bridge
endif

ifeq ($(CACHEFLUSH_TYPE),CACHEFLUSH_GENERIC)
ccflags-y += -I$(bridge_base)/cachegeneric_bridge
endif

ifeq ($(SUPPORT_SECURE_EXPORT),1)
ccflags-y += \
 -I$(bridge_base)/syncsexport_bridge \
 -I$(bridge_base)/smm_bridge
endif
ifeq ($(SUPPORT_INSECURE_EXPORT),1)
ccflags-y += \
 -I$(bridge_base)/syncexport_bridge
endif

ifeq ($(SUPPORT_ION),1)
ccflags-y += -I$(bridge_base)/dmabuf_bridge
endif

ifeq ($(SUPPORT_ANDROID_PLATFORM),1)
ccflags-y += -I$(TOP)/kernel/drivers/staging/imgtec
endif

ifeq ($(SUPPORT_NATIVE_FENCE_SYNC),1)
pvrsrvkm-y += kernel/drivers/staging/imgtec/pvr_sync.o
endif

ifeq ($(SUPPORT_VALIDATION),1)
ccflags-y += -I$(bridge_base)/validation_bridge
endif

ifeq ($(PVR_TESTING_UTILS),1)
ccflags-y += -I$(bridge_base)/tutils_bridge
endif

pvrsrvkm-y += \
 generated/mm_bridge/server_mm_bridge.o \
 generated/dmm_bridge/client_mm_bridge.o \
 generated/pdumpmm_bridge/server_pdumpmm_bridge.o \
 generated/dpdumpmm_bridge/client_pdumpmm_bridge.o \
 generated/cmm_bridge/server_cmm_bridge.o \
 generated/pdump_bridge/server_pdump_bridge.o \
 generated/pdumpctrl_bridge/server_pdumpctrl_bridge.o \
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

ifeq ($(SUPPORT_KERNEL_SRVINIT),1)
pvrsrvkm-y += \
 generated/dsrvcore_bridge/client_srvcore_bridge.o \
 generated/drgxinit_bridge/client_rgxinit_bridge.o \
 generated/dpdumpctrl_bridge/client_pdumpctrl_bridge.o \
 generated/dpdump_bridge/client_pdump_bridge.o \
 generated/drgxpdump_bridge/client_rgxpdump_bridge.o
endif

ifeq ($(PVR_RI_DEBUG),1)
pvrsrvkm-y += \
 generated/ri_bridge/server_ri_bridge.o \
 generated/dri_bridge/client_ri_bridge.o
endif
ifeq ($(SUPPORT_PAGE_FAULT_DEBUG),1)
pvrsrvkm-y += \
 generated/devicememhistory_bridge/server_devicememhistory_bridge.o \
 generated/ddevicememhistory_bridge/client_devicememhistory_bridge.o
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

ifeq ($(SUPPORT_VALIDATION),1)
pvrsrvkm-y += generated/validation_bridge/server_validation_bridge.o
pvrsrvkm-y += services/server/common/validation.o
endif # SUPPORT_VALIDATION

ifeq ($(PVR_TESTING_UTILS),1)
pvrsrvkm-y += generated/tutils_bridge/server_tutils_bridge.o
endif

ifneq ($(W),1)
CFLAGS_server_mm_bridge.o := -Werror
CFLAGS_server_cmm_bridge.o := -Werror
CFLAGS_client_mm_bridge.o := -Werror
CFLAGS_client_pdumpmm_bridge.o := -Werror
CFLAGS_server_pdumpctrl_bridge.o := -Werror
CFLAGS_server_sync_bridge.o := -Werror
CFLAGS_server_rgxtq_bridge.o := -Werror
CFLAGS_server_rgxinit_bridge.o := -Werror
CFLAGS_server_rgxta3d_bridge.o := -Werror
CFLAGS_server_rgxcmp_bridge.o := -Werror
CFLAGS_server_srvcore_bridge.o := -Werror
CFLAGS_server_breakpoint_bridge.o := -Werror
CFLAGS_server_debugmisc_bridge.o := -Werror
CFLAGS_server_rgxpdump_bridge.o := -Werror
CFLAGS_server_pdumpmm_bridge.o := -Werror
CFLAGS_client_pdumpmm_bridge.o := -Werror
CFLAGS_server_pdump_bridge.o := -Werror
CFLAGS_server_rgxray_bridge.o := -Werror
CFLAGS_server_regconfig_bridge.o := -Werror
CFLAGS_server_timerquery_bridge.o := -Werror

ifeq ($(SUPPORT_KERNEL_SRVINIT),1)
CFLAGS_client_srvcore_bridge.o := -Werror
endif
ifeq ($(SUPPORT_DISPLAY_CLASS),1)
CFLAGS_server_dc_bridge.o := -Werror
endif
ifeq ($(CACHEFLUSH_TYPE),CACHEFLUSH_GENERIC)
CFLAGS_cache_generic.o := -Werror
CFLAGS_server_cachegeneric_bridge.o := -Werror
endif
ifeq ($(SUPPORT_SECURE_EXPORT),1)
CFLAGS_ossecure_export.o := -Werror
CFLAGS_server_smm_bridge.o := -Werror
CFLAGS_server_syncsexport_bridge.o := -Werror
endif
ifeq ($(SUPPORT_INSECURE_EXPORT),1)
CFLAGS_server_syncexport_bridge.o := -Werror
endif
CFLAGS_server_pvrtl_bridge.o := -Werror
CFLAGS_client_pvrtl_bridge.o := -Werror
CFLAGS_server_rgxhwperf_bridge.o := -Werror
ifeq ($(PVR_RI_DEBUG),1)
CFLAGS_server_ri_bridge.o := -Werror
CFLAGS_client_ri_bridge.o := -Werror
endif
ifeq ($(SUPPORT_PAGE_FAULT_DEBUG),1)
CFLAGS_server_devicememhistory_bridge.o := -Werror
CFLAGS_client_devicememhistory_bridge.o := -Werror
endif
ifeq ($(SUPPORT_ION),1)
CFLAGS_physmem_dmabuf.o := -Werror
CFLAGS_server_dmabuf_bridge.o = -Werror
endif
ifeq ($(SUPPORT_VALIDATION),1)
CFLAGS_server_validation_bridge.o := -Werror
endif
ifeq ($(PVR_TESTING_UTILS),1)
CFLAGS_server_tutils_bridge.o := -Werror
endif
endif

ifeq ($(SUPPORT_DRM),1)
ifneq ($(SUPPORT_DRM_EXT),1)
pvrsrvkm-y += \
 services/server/env/linux/pvr_drm.o
endif

pvrsrvkm-y += \
 services/server/env/linux/pvr_drm_gem.o \
 services/server/env/linux/pvr_drm_prime.o \
 services/server/env/linux/physmem_dmabuf.o

CFLAGS_physmem_dmabuf.o := -Werror

ifeq ($(SUPPORT_DRM_DC_MODULE),1)
pvrsrvkm-y += \
 services/server/env/linux/pvr_drm_display.o
ifneq ($(SUPPORT_DISPLAY_CLASS),1)
pvrsrvkm-y += \
 services/server/common/scp.o
CFLAGS_scp.o := -Werror
endif
endif

ccflags-y += \
 -Iinclude/drm \
 -I$(TOP)/services/include/env/linux
endif # SUPPORT_DRM

ifeq ($(SUPPORT_KERNEL_SRVINIT),1)
SRVINIT := srvinit
include $(TOP)/services/srvinit/env/linux/srvinit_common.mk
pvrsrvkm-y += $(patsubst $(TOP)/%.c,%.o,$($(SRVINIT)_src))
ccflags-y += $(patsubst %,-I$(TOP)/%,$($(SRVINIT)_includes))

pvrsrvkm-y += \
 services/client/common/pvrsrv_devmem.o \
 services/client/common/srvcore.o \
 services/client/common/connection.o \
 services/client/devices/rgx/rgxinit_client.o \
 services/client/env/linux/km/osfunc_km.o \
 services/client/env/linux/km/osfunc_mutex.o \
 services/client/env/linux/km/pvr_bridge_client.o \
 services/client/env/linux/km/pvrtl_client.o

ifeq ($(PDUMP),1)
pvrsrvkm-y += \
 services/client/common/pvrsrv_devmem_pdump.o \
 services/client/common/pvr_pdump_glue.o \
 services/client/common/devicemem_pdumpc.o
endif

ifeq ($(SUPPORT_VALIDATION),1)
pvrsrvkm-y += \
 services/client/common/validation.o
endif

ccflags-y += \
 -I$(TOP)/services/client/include \
 -I$(TOP)/services/client/common \
 -I$(TOP)/services/client/env/linux

ccflags-y += \
 -I$(OUT)/target_neutral/intermediates/firmware
endif

include $(TOP)/services/system/$(PVR_SYSTEM)/Kbuild.mk
