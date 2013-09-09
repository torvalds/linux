
SGX_FILE_PREFIX := drivers/gpu/pvr
#
#GLOBAL OPTION
#

USING_ION=1
USING_DMA_BUF=1

ccflags-y := \
	-Wno-pointer-sign\
	-fno-strict-overflow\
	-fconserve-stack\
	-Wdeclaration-after-statement\
	-Wpointer-arith\
	-Wmissing-format-attribute\
	-Wno-format-zero-length\
	-Wmissing-prototypes\
	-Wstrict-prototypes\
	-Wno-unused-parameter\
	-Wno-sign-compare\
	-Werror\
	-fno-strict-aliasing\
	-Wno-pointer-arith\
	-Os

ccflags-y += -DLINUX -D__linux__ -DANDROID

ifeq ($(USING_ION), 1)
DEFINES += -DSUPPORT_ION
pvrsrvkm-y += \
	services4/srvkm/env/linux/ion.o
endif

ifeq ($(USING_DMA_BUF), 1)
DEFINES += -DEXYNOS_ION_DMA_BUFFER_FD
endif

INCLUDES = \
	-I$(SGX_FILE_PREFIX)\
	-I$(SGX_FILE_PREFIX)/include4 \
	-I$(SGX_FILE_PREFIX)/services4/system/exynos5410 \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/env/linux/ \
	-I$(SGX_FILE_PREFIX)/tools/intern/debug/dbgdriv/common/ \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/bridged/ \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/bridged/sgx/ \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/devices/sgx/ \
	-I$(SGX_FILE_PREFIX)/services4/include \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/include/ \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/common/ \
	-I$(SGX_FILE_PREFIX)/services4/srvkm/hwdefs/ \
	-I$(SGX_FILE_PREFIX)/services4/system/include

DEFINES += \
	-DSUPPORT_SGX_NEW_STATUS_VALS \
	-DSYS_USING_INTERRUPTS \
	-DPVR_SECURE_HANDLES \
	-DSUPPORT_DBGDRV_EVENT_OBJECTS \
	-DTRANSFER_QUEUE \
	-DSUPPORT_SGX \
	-DLDM_PLATFORM \
	-DPVR_LINUX_USING_WORKQUEUES \
	-DPVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE \
	-DPVR_LINUX_TIMERS_USING_WORKQUEUES \
	-DSYS_CUSTOM_POWERLOCK_WRAP \
	-DPVR_BUILD_DATE="" \
	-DPVRSRV_MODNAME="\"pvrsrvkm\"" \
 	-DSUPPORT_PERCONTEXT_PB \
 	-DSUPPORT_SGX_LOW_LATENCY_SCHEDULING \
 	-DPVR_NO_FULL_CACHE_OPS \
 	-DPVRSRV_NEED_PVR_DPF \
 	-DPVRSRV_NEED_PVR_ASSERT \
	-DSUPPORT_HW_RECOVERY \
	-DSUPPORT_MEMINFO_IDS \
	-DSYS_SGX_ACTIVE_POWER_LATENCY_MS=50 \
	-DSUPPORT_PVRSRV_GET_DC_SYSTEM_BUFFER \
	-DSUPPORT_LINUX_X86_WRITECOMBINE \
	-DSUPPORT_LINUX_X86_PAT \
	-DSUPPORT_LARGE_GENERAL_HEAP \
	-DSUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED \
	-DSUPPORT_ANDROID_SYNC \
	-DUSING_640MHZ \
	-DSUPPORT_SGX_HWPERF \
	-DDUMP_UKERNEL_INFO_AT_TIMEOUT

ifeq ($(CONFIG_PVR_SGX_ACTIVE_POWER_MANAGEMENT),y)
DEFINES += \
	-DSUPPORT_ACTIVE_POWER_MANAGEMENT
endif

#TODO : check this option
#   -DPVR_NO_FULL_CACHE_OPS - can we support this?
#	-DPVR2D_ALT_2DHW - this is defined in UM only
#	-DPVRSRV_RESET_ON_HWTIMEOUT
#   -DSUPPORT_LINUX_X86_PAT
#	-DPVR_LINUX_TIMERS_USING_WORKQUEUES

ifeq ($(CONFIG_SOC_EXYNOS5410),y)
DEFINES += \
	-DSGX_FEATURE_MP \
	-DSGX_FEATURE_MP_CORE_COUNT=3 \
	-DSGX_FEATURE_SYSTEM_CACHE

endif

ccflags-y += -I$(INCLUDES) \
	$(DEFINES)

ccflags-$(CONFIG_PVR_BUILD_RELEASE) += \
	-DPVR_BUILD_TYPE="\"release\"" -DRELEASE

ccflags-$(CONFIG_PVR_BUILD_DEBUG) += \
	-DPVR_BUILD_TYPE="\"debug\"" -DDEBUG \
	-DDEBUG_LINUX_MEMORY_ALLOCATIONS \
	-DDEBUG_LINUX_MEM_AREAS \
	-DDEBUG_LINUX_MMAP_AREAS \
	-DDEBUG_BRIDGE_KM \
	-DSUPPORT_DBGDRV_EVENT_OBJECTS

pvrsrvkm-y += \
	tools/intern/debug/dbgdriv/common/handle.o \
	services4/srvkm/env/linux/mmap.o \
	services4/srvkm/env/linux/osfunc.o \
	services4/srvkm/env/linux/mm.o \
	services4/srvkm/env/linux/pvr_bridge_k.o \
	services4/srvkm/env/linux/pdump.o \
	services4/srvkm/env/linux/pvr_debug.o \
	services4/srvkm/env/linux/mutex.o \
	services4/srvkm/env/linux/proc.o \
	services4/srvkm/env/linux/module.o \
	services4/srvkm/env/linux/osperproc.o \
	services4/srvkm/env/linux/event.o \
	services4/srvkm/env/linux/mutils.o \
	services4/srvkm/bridged/bridged_pvr_bridge.o \
	services4/srvkm/bridged/sgx/bridged_sgx_bridge.o \
	services4/srvkm/bridged/bridged_support.o \
	services4/srvkm/devices/sgx/sgxreset.o \
	services4/srvkm/devices/sgx/mmu.o \
	services4/srvkm/devices/sgx/sgxinit.o \
	services4/srvkm/devices/sgx/sgxutils.o \
	services4/srvkm/devices/sgx/sgxkick.o \
	services4/srvkm/devices/sgx/sgxpower.o \
	services4/srvkm/devices/sgx/pb.o \
	services4/srvkm/devices/sgx/sgxtransfer.o \
	services4/srvkm/common/buffer_manager.o \
	services4/srvkm/common/ra.o \
	services4/srvkm/common/refcount.o \
	services4/srvkm/common/queue.o \
	services4/srvkm/common/metrics.o \
	services4/srvkm/common/deviceclass.o \
	services4/srvkm/common/power.o \
	services4/srvkm/common/perproc.o \
	services4/srvkm/common/osfunc_common.o \
	services4/srvkm/common/mem_debug.o \
	services4/srvkm/common/pvrsrv.o \
	services4/srvkm/common/handle.o \
	services4/srvkm/common/mem.o \
	services4/srvkm/common/pdump_common.o \
	services4/srvkm/common/hash.o \
	services4/srvkm/common/lists.o \
	services4/srvkm/common/devicemem.o \
	services4/srvkm/common/resman.o \
	services4/srvkm/env/linux/pvr_sync.o

ccflags-$(CONFIG_CPU_S5PC110) += \
	-DPVR_BUILD_DIR="\"pvr\"" \
	-DDISPLAY_CONTROLLER=s3c_lcd \
    -DSLSI_S5PC110 \
	-DSGX540 -DSUPPORT_SGX540 \
	-DSGX_CORE_REV=120

pvrsrvkm-$(CONFIG_CPU_S5PC110) += \
	services4/system/s5pc110/sysconfig.o \
	services4/system/s5pc110/sysutils.o

ccflags-$(CONFIG_SOC_EXYNOS5410) += \
	-DPVR_BUILD_DIR="\"pvr\"" \
	-DDISPLAY_CONTROLLER=s3c_lcd \
    -DSLSI_EXYNOS5410 \
	-DSGX544 -DSUPPORT_SGX544 \
	-DSGX_CORE_REV=115

pvrsrvkm-$(CONFIG_SOC_EXYNOS5410) += \
	services4/system/exynos5410/sysconfig.o \
	services4/system/exynos5410/sec_power.o \
	services4/system/exynos5410/sec_control_pwr_clk.o \
	services4/system/exynos5410/sec_clock.o

ifeq ($(CONFIG_SOC_EXYNOS5410),y)
	pvrsrvkm-$(CONFIG_PVR_SGX_DVFS) += \
	services4/system/exynos5410/secutils.o \
	services4/system/exynos5410/sec_dvfs.o

ccflags-$(CONFIG_PVR_SGX_DVFS) += \
	-DSYS_SUPPORTS_SGX_IDLE_CALLBACK
endif


obj-$(CONFIG_PVR_PDUMP) += dbgdrv

obj-$(CONFIG_PVR_SGX) += pvrsrvkm.o

s3c_lcd-y := \
	services4/3rdparty/s3c_lcd/s3c_displayclass.o \
	services4/3rdparty/s3c_lcd/s3c_lcd.o

obj-$(CONFIG_PVR_SGX) += s3c_lcd.o
