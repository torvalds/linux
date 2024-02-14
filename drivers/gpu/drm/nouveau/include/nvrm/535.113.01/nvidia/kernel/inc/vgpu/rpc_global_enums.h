#ifndef __src_nvidia_kernel_inc_vgpu_rpc_global_enums_h__
#define __src_nvidia_kernel_inc_vgpu_rpc_global_enums_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#ifndef X
#    define X(UNIT, RPC) NV_VGPU_MSG_FUNCTION_##RPC,
#    define DEFINING_X_IN_RPC_GLOBAL_ENUMS_H
enum {
#endif
    X(RM, NOP)                             // 0
    X(RM, SET_GUEST_SYSTEM_INFO)           // 1
    X(RM, ALLOC_ROOT)                      // 2
    X(RM, ALLOC_DEVICE)                    // 3 deprecated
    X(RM, ALLOC_MEMORY)                    // 4
    X(RM, ALLOC_CTX_DMA)                   // 5
    X(RM, ALLOC_CHANNEL_DMA)               // 6
    X(RM, MAP_MEMORY)                      // 7
    X(RM, BIND_CTX_DMA)                    // 8 deprecated
    X(RM, ALLOC_OBJECT)                    // 9
    X(RM, FREE)                            //10
    X(RM, LOG)                             //11
    X(RM, ALLOC_VIDMEM)                    //12
    X(RM, UNMAP_MEMORY)                    //13
    X(RM, MAP_MEMORY_DMA)                  //14
    X(RM, UNMAP_MEMORY_DMA)                //15
    X(RM, GET_EDID)                        //16
    X(RM, ALLOC_DISP_CHANNEL)              //17
    X(RM, ALLOC_DISP_OBJECT)               //18
    X(RM, ALLOC_SUBDEVICE)                 //19
    X(RM, ALLOC_DYNAMIC_MEMORY)            //20
    X(RM, DUP_OBJECT)                      //21
    X(RM, IDLE_CHANNELS)                   //22
    X(RM, ALLOC_EVENT)                     //23
    X(RM, SEND_EVENT)                      //24
    X(RM, REMAPPER_CONTROL)                //25 deprecated
    X(RM, DMA_CONTROL)                     //26
    X(RM, DMA_FILL_PTE_MEM)                //27
    X(RM, MANAGE_HW_RESOURCE)              //28
    X(RM, BIND_ARBITRARY_CTX_DMA)          //29 deprecated
    X(RM, CREATE_FB_SEGMENT)               //30
    X(RM, DESTROY_FB_SEGMENT)              //31
    X(RM, ALLOC_SHARE_DEVICE)              //32
    X(RM, DEFERRED_API_CONTROL)            //33
    X(RM, REMOVE_DEFERRED_API)             //34
    X(RM, SIM_ESCAPE_READ)                 //35
    X(RM, SIM_ESCAPE_WRITE)                //36
    X(RM, SIM_MANAGE_DISPLAY_CONTEXT_DMA)  //37
    X(RM, FREE_VIDMEM_VIRT)                //38
    X(RM, PERF_GET_PSTATE_INFO)            //39  deprecated for vGPU, used by GSP
    X(RM, PERF_GET_PERFMON_SAMPLE)         //40
    X(RM, PERF_GET_VIRTUAL_PSTATE_INFO)    //41  deprecated
    X(RM, PERF_GET_LEVEL_INFO)             //42
    X(RM, MAP_SEMA_MEMORY)                 //43
    X(RM, UNMAP_SEMA_MEMORY)               //44
    X(RM, SET_SURFACE_PROPERTIES)          //45
    X(RM, CLEANUP_SURFACE)                 //46
    X(RM, UNLOADING_GUEST_DRIVER)          //47
    X(RM, TDR_SET_TIMEOUT_STATE)           //48
    X(RM, SWITCH_TO_VGA)                   //49
    X(RM, GPU_EXEC_REG_OPS)                //50
    X(RM, GET_STATIC_INFO)                 //51
    X(RM, ALLOC_VIRTMEM)                   //52
    X(RM, UPDATE_PDE_2)                    //53
    X(RM, SET_PAGE_DIRECTORY)              //54
    X(RM, GET_STATIC_PSTATE_INFO)          //55
    X(RM, TRANSLATE_GUEST_GPU_PTES)        //56
    X(RM, RESERVED_57)                     //57
    X(RM, RESET_CURRENT_GR_CONTEXT)        //58
    X(RM, SET_SEMA_MEM_VALIDATION_STATE)   //59
    X(RM, GET_ENGINE_UTILIZATION)          //60
    X(RM, UPDATE_GPU_PDES)                 //61
    X(RM, GET_ENCODER_CAPACITY)            //62
    X(RM, VGPU_PF_REG_READ32)              //63
    X(RM, SET_GUEST_SYSTEM_INFO_EXT)       //64
    X(GSP, GET_GSP_STATIC_INFO)            //65
    X(RM, RMFS_INIT)                       //66
    X(RM, RMFS_CLOSE_QUEUE)                //67
    X(RM, RMFS_CLEANUP)                    //68
    X(RM, RMFS_TEST)                       //69
    X(RM, UPDATE_BAR_PDE)                  //70
    X(RM, CONTINUATION_RECORD)             //71
    X(RM, GSP_SET_SYSTEM_INFO)             //72
    X(RM, SET_REGISTRY)                    //73
    X(GSP, GSP_INIT_POST_OBJGPU)           //74 deprecated
    X(RM, SUBDEV_EVENT_SET_NOTIFICATION)   //75 deprecated
    X(GSP, GSP_RM_CONTROL)                 //76
    X(RM, GET_STATIC_INFO2)                //77
    X(RM, DUMP_PROTOBUF_COMPONENT)         //78
    X(RM, UNSET_PAGE_DIRECTORY)            //79
    X(RM, GET_CONSOLIDATED_STATIC_INFO)    //80
    X(RM, GMMU_REGISTER_FAULT_BUFFER)      //81 deprecated
    X(RM, GMMU_UNREGISTER_FAULT_BUFFER)    //82 deprecated
    X(RM, GMMU_REGISTER_CLIENT_SHADOW_FAULT_BUFFER)   //83 deprecated
    X(RM, GMMU_UNREGISTER_CLIENT_SHADOW_FAULT_BUFFER) //84 deprecated
    X(RM, CTRL_SET_VGPU_FB_USAGE)          //85
    X(RM, CTRL_NVFBC_SW_SESSION_UPDATE_INFO)    //86
    X(RM, CTRL_NVENC_SW_SESSION_UPDATE_INFO)    //87
    X(RM, CTRL_RESET_CHANNEL)                   //88
    X(RM, CTRL_RESET_ISOLATED_CHANNEL)          //89
    X(RM, CTRL_GPU_HANDLE_VF_PRI_FAULT)         //90
    X(RM, CTRL_CLK_GET_EXTENDED_INFO)           //91
    X(RM, CTRL_PERF_BOOST)                      //92
    X(RM, CTRL_PERF_VPSTATES_GET_CONTROL)       //93
    X(RM, CTRL_GET_ZBC_CLEAR_TABLE)             //94
    X(RM, CTRL_SET_ZBC_COLOR_CLEAR)             //95
    X(RM, CTRL_SET_ZBC_DEPTH_CLEAR)             //96
    X(RM, CTRL_GPFIFO_SCHEDULE)                 //97
    X(RM, CTRL_SET_TIMESLICE)                   //98
    X(RM, CTRL_PREEMPT)                         //99
    X(RM, CTRL_FIFO_DISABLE_CHANNELS)           //100
    X(RM, CTRL_SET_TSG_INTERLEAVE_LEVEL)        //101
    X(RM, CTRL_SET_CHANNEL_INTERLEAVE_LEVEL)    //102
    X(GSP, GSP_RM_ALLOC)                        //103
    X(RM, CTRL_GET_P2P_CAPS_V2)                 //104
    X(RM, CTRL_CIPHER_AES_ENCRYPT)              //105
    X(RM, CTRL_CIPHER_SESSION_KEY)              //106
    X(RM, CTRL_CIPHER_SESSION_KEY_STATUS)       //107
    X(RM, CTRL_DBG_CLEAR_ALL_SM_ERROR_STATES)   //108
    X(RM, CTRL_DBG_READ_ALL_SM_ERROR_STATES)    //109
    X(RM, CTRL_DBG_SET_EXCEPTION_MASK)          //110
    X(RM, CTRL_GPU_PROMOTE_CTX)                 //111
    X(RM, CTRL_GR_CTXSW_PREEMPTION_BIND)        //112
    X(RM, CTRL_GR_SET_CTXSW_PREEMPTION_MODE)    //113
    X(RM, CTRL_GR_CTXSW_ZCULL_BIND)             //114
    X(RM, CTRL_GPU_INITIALIZE_CTX)              //115
    X(RM, CTRL_VASPACE_COPY_SERVER_RESERVED_PDES)    //116
    X(RM, CTRL_FIFO_CLEAR_FAULTED_BIT)          //117
    X(RM, CTRL_GET_LATEST_ECC_ADDRESSES)        //118
    X(RM, CTRL_MC_SERVICE_INTERRUPTS)           //119
    X(RM, CTRL_DMA_SET_DEFAULT_VASPACE)         //120
    X(RM, CTRL_GET_CE_PCE_MASK)                 //121
    X(RM, CTRL_GET_ZBC_CLEAR_TABLE_ENTRY)       //122
    X(RM, CTRL_GET_NVLINK_PEER_ID_MASK)         //123
    X(RM, CTRL_GET_NVLINK_STATUS)               //124
    X(RM, CTRL_GET_P2P_CAPS)                    //125
    X(RM, CTRL_GET_P2P_CAPS_MATRIX)             //126
    X(RM, RESERVED_0)                           //127
    X(RM, CTRL_RESERVE_PM_AREA_SMPC)            //128
    X(RM, CTRL_RESERVE_HWPM_LEGACY)             //129
    X(RM, CTRL_B0CC_EXEC_REG_OPS)               //130
    X(RM, CTRL_BIND_PM_RESOURCES)               //131
    X(RM, CTRL_DBG_SUSPEND_CONTEXT)             //132
    X(RM, CTRL_DBG_RESUME_CONTEXT)              //133
    X(RM, CTRL_DBG_EXEC_REG_OPS)                //134
    X(RM, CTRL_DBG_SET_MODE_MMU_DEBUG)          //135
    X(RM, CTRL_DBG_READ_SINGLE_SM_ERROR_STATE)  //136
    X(RM, CTRL_DBG_CLEAR_SINGLE_SM_ERROR_STATE) //137
    X(RM, CTRL_DBG_SET_MODE_ERRBAR_DEBUG)       //138
    X(RM, CTRL_DBG_SET_NEXT_STOP_TRIGGER_TYPE)  //139
    X(RM, CTRL_ALLOC_PMA_STREAM)                //140
    X(RM, CTRL_PMA_STREAM_UPDATE_GET_PUT)       //141
    X(RM, CTRL_FB_GET_INFO_V2)                  //142
    X(RM, CTRL_FIFO_SET_CHANNEL_PROPERTIES)     //143
    X(RM, CTRL_GR_GET_CTX_BUFFER_INFO)          //144
    X(RM, CTRL_KGR_GET_CTX_BUFFER_PTES)         //145
    X(RM, CTRL_GPU_EVICT_CTX)                   //146
    X(RM, CTRL_FB_GET_FS_INFO)                  //147
    X(RM, CTRL_GRMGR_GET_GR_FS_INFO)            //148
    X(RM, CTRL_STOP_CHANNEL)                    //149
    X(RM, CTRL_GR_PC_SAMPLING_MODE)             //150
    X(RM, CTRL_PERF_RATED_TDP_GET_STATUS)       //151
    X(RM, CTRL_PERF_RATED_TDP_SET_CONTROL)      //152
    X(RM, CTRL_FREE_PMA_STREAM)                 //153
    X(RM, CTRL_TIMER_SET_GR_TICK_FREQ)          //154
    X(RM, CTRL_FIFO_SETUP_VF_ZOMBIE_SUBCTX_PDB) //155
    X(RM, GET_CONSOLIDATED_GR_STATIC_INFO)      //156
    X(RM, CTRL_DBG_SET_SINGLE_SM_SINGLE_STEP)   //157
    X(RM, CTRL_GR_GET_TPC_PARTITION_MODE)       //158
    X(RM, CTRL_GR_SET_TPC_PARTITION_MODE)       //159
    X(UVM, UVM_PAGING_CHANNEL_ALLOCATE)         //160
    X(UVM, UVM_PAGING_CHANNEL_DESTROY)          //161
    X(UVM, UVM_PAGING_CHANNEL_MAP)              //162
    X(UVM, UVM_PAGING_CHANNEL_UNMAP)            //163
    X(UVM, UVM_PAGING_CHANNEL_PUSH_STREAM)      //164
    X(UVM, UVM_PAGING_CHANNEL_SET_HANDLES)      //165
    X(UVM, UVM_METHOD_STREAM_GUEST_PAGES_OPERATION)  //166
    X(RM, CTRL_INTERNAL_QUIESCE_PMA_CHANNEL)    //167
    X(RM, DCE_RM_INIT)                          //168
    X(RM, REGISTER_VIRTUAL_EVENT_BUFFER)        //169
    X(RM, CTRL_EVENT_BUFFER_UPDATE_GET)         //170
    X(RM, GET_PLCABLE_ADDRESS_KIND)             //171
    X(RM, CTRL_PERF_LIMITS_SET_STATUS_V2)       //172
    X(RM, CTRL_INTERNAL_SRIOV_PROMOTE_PMA_STREAM)    //173
    X(RM, CTRL_GET_MMU_DEBUG_MODE)              //174
    X(RM, CTRL_INTERNAL_PROMOTE_FAULT_METHOD_BUFFERS) //175
    X(RM, CTRL_FLCN_GET_CTX_BUFFER_SIZE)        //176
    X(RM, CTRL_FLCN_GET_CTX_BUFFER_INFO)        //177
    X(RM, DISABLE_CHANNELS)                     //178
    X(RM, CTRL_FABRIC_MEMORY_DESCRIBE)          //179
    X(RM, CTRL_FABRIC_MEM_STATS)                //180
    X(RM, SAVE_HIBERNATION_DATA)                //181
    X(RM, RESTORE_HIBERNATION_DATA)             //182
    X(RM, CTRL_INTERNAL_MEMSYS_SET_ZBC_REFERENCED) //183
    X(RM, CTRL_EXEC_PARTITIONS_CREATE)          //184
    X(RM, CTRL_EXEC_PARTITIONS_DELETE)          //185
    X(RM, CTRL_GPFIFO_GET_WORK_SUBMIT_TOKEN)    //186
    X(RM, CTRL_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX) //187
    X(RM, PMA_SCRUBBER_SHARED_BUFFER_GUEST_PAGES_OPERATION)  //188
    X(RM, CTRL_MASTER_GET_VIRTUAL_FUNCTION_ERROR_CONT_INTR_MASK)    //189
    X(RM, SET_SYSMEM_DIRTY_PAGE_TRACKING_BUFFER)  //190
    X(RM, CTRL_SUBDEVICE_GET_P2P_CAPS)          // 191
    X(RM, CTRL_BUS_SET_P2P_MAPPING)             // 192
    X(RM, CTRL_BUS_UNSET_P2P_MAPPING)           // 193
    X(RM, CTRL_FLA_SETUP_INSTANCE_MEM_BLOCK)    // 194
    X(RM, CTRL_GPU_MIGRATABLE_OPS)              // 195
    X(RM, CTRL_GET_TOTAL_HS_CREDITS)            // 196
    X(RM, CTRL_GET_HS_CREDITS)                  // 197
    X(RM, CTRL_SET_HS_CREDITS)                  // 198
    X(RM, CTRL_PM_AREA_PC_SAMPLER)              // 199
    X(RM, INVALIDATE_TLB)                       // 200
    X(RM, NUM_FUNCTIONS)                        //END
#ifdef DEFINING_X_IN_RPC_GLOBAL_ENUMS_H
};
#   undef X
#   undef DEFINING_X_IN_RPC_GLOBAL_ENUMS_H
#endif

#ifndef E
#    define E(RPC) NV_VGPU_MSG_EVENT_##RPC,
#    define DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
enum {
#endif
    E(FIRST_EVENT = 0x1000)                      // 0x1000
    E(GSP_INIT_DONE)                             // 0x1001
    E(GSP_RUN_CPU_SEQUENCER)                     // 0x1002
    E(POST_EVENT)                                // 0x1003
    E(RC_TRIGGERED)                              // 0x1004
    E(MMU_FAULT_QUEUED)                          // 0x1005
    E(OS_ERROR_LOG)                              // 0x1006
    E(RG_LINE_INTR)                              // 0x1007
    E(GPUACCT_PERFMON_UTIL_SAMPLES)              // 0x1008
    E(SIM_READ)                                  // 0x1009
    E(SIM_WRITE)                                 // 0x100a
    E(SEMAPHORE_SCHEDULE_CALLBACK)               // 0x100b
    E(UCODE_LIBOS_PRINT)                         // 0x100c
    E(VGPU_GSP_PLUGIN_TRIGGERED)                 // 0x100d
    E(PERF_GPU_BOOST_SYNC_LIMITS_CALLBACK)       // 0x100e
    E(PERF_BRIDGELESS_INFO_UPDATE)               // 0x100f
    E(VGPU_CONFIG)                               // 0x1010
    E(DISPLAY_MODESET)                           // 0x1011
    E(EXTDEV_INTR_SERVICE)                       // 0x1012
    E(NVLINK_INBAND_RECEIVED_DATA_256)           // 0x1013
    E(NVLINK_INBAND_RECEIVED_DATA_512)           // 0x1014
    E(NVLINK_INBAND_RECEIVED_DATA_1024)          // 0x1015
    E(NVLINK_INBAND_RECEIVED_DATA_2048)          // 0x1016
    E(NVLINK_INBAND_RECEIVED_DATA_4096)          // 0x1017
    E(TIMED_SEMAPHORE_RELEASE)                   // 0x1018
    E(NVLINK_IS_GPU_DEGRADED)                    // 0x1019
    E(PFM_REQ_HNDLR_STATE_SYNC_CALLBACK)         // 0x101a
    E(GSP_SEND_USER_SHARED_DATA)                 // 0x101b
    E(NVLINK_FAULT_UP)                           // 0x101c
    E(GSP_LOCKDOWN_NOTICE)                       // 0x101d
    E(MIG_CI_CONFIG_UPDATE)                      // 0x101e
    E(NUM_EVENTS)                                // END
#ifdef DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
};
#   undef E
#   undef DEFINING_E_IN_RPC_GLOBAL_ENUMS_H
#endif

#endif
