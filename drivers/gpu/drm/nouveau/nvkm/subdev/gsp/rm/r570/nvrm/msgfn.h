/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_MSGFN_H__
#define __NVRM_MSGFN_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

#ifndef E
#    define E(RPC, VAL) NV_VGPU_MSG_EVENT_##RPC = VAL,
#    define DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
enum {
#endif
    E(FIRST_EVENT,                                            0x1000)
    E(GSP_INIT_DONE,                                          0x1001)
    E(GSP_RUN_CPU_SEQUENCER,                                  0x1002)
    E(POST_EVENT,                                             0x1003)
    E(RC_TRIGGERED,                                           0x1004)
    E(MMU_FAULT_QUEUED,                                       0x1005)
    E(OS_ERROR_LOG,                                           0x1006)
    E(RG_LINE_INTR,                                           0x1007)
    E(GPUACCT_PERFMON_UTIL_SAMPLES,                           0x1008)
    E(SIM_READ,                                               0x1009)
    E(SIM_WRITE,                                              0x100a)
    E(SEMAPHORE_SCHEDULE_CALLBACK,                            0x100b)
    E(UCODE_LIBOS_PRINT,                                      0x100c)
    E(VGPU_GSP_PLUGIN_TRIGGERED,                              0x100d)
    E(PERF_GPU_BOOST_SYNC_LIMITS_CALLBACK,                    0x100e)
    E(PERF_BRIDGELESS_INFO_UPDATE,                            0x100f)
    E(VGPU_CONFIG,                                            0x1010)
    E(DISPLAY_MODESET,                                        0x1011)
    E(EXTDEV_INTR_SERVICE,                                    0x1012)
    E(NVLINK_INBAND_RECEIVED_DATA_256,                        0x1013)
    E(NVLINK_INBAND_RECEIVED_DATA_512,                        0x1014)
    E(NVLINK_INBAND_RECEIVED_DATA_1024,                       0x1015)
    E(NVLINK_INBAND_RECEIVED_DATA_2048,                       0x1016)
    E(NVLINK_INBAND_RECEIVED_DATA_4096,                       0x1017)
    E(TIMED_SEMAPHORE_RELEASE,                                0x1018)
    E(NVLINK_IS_GPU_DEGRADED,                                 0x1019)
    E(PFM_REQ_HNDLR_STATE_SYNC_CALLBACK,                      0x101a)
    E(NVLINK_FAULT_UP,                                        0x101b)
    E(GSP_LOCKDOWN_NOTICE,                                    0x101c)
    E(MIG_CI_CONFIG_UPDATE,                                   0x101d)
    E(UPDATE_GSP_TRACE,                                       0x101e)
    E(NVLINK_FATAL_ERROR_RECOVERY,                            0x101f)
    E(GSP_POST_NOCAT_RECORD,                                  0x1020)
    E(FECS_ERROR,                                             0x1021)
    E(RECOVERY_ACTION,                                        0x1022)
    E(NUM_EVENTS,                                             0x1023)
#ifdef DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
};
#   undef E
#   undef DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
#endif
#endif
