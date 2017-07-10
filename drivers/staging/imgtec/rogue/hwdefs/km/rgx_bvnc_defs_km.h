/*************************************************************************/ /*!
@Title          Hardware definition file rgx_bvnc_defs_km.h
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/**************************************************
*       Auto generated file by BVNCTableGen.py    *
*       This file should not be edited manually   *
**************************************************/

#ifndef _RGX_BVNC_DEFS_KM_H_
#define _RGX_BVNC_DEFS_KM_H_

#include "img_types.h"

#define   BVNC_FIELD_WIDTH  (16U)

#define	RGX_FEATURE_AXI_ACELITE_POS                                 	(0U)
#define	RGX_FEATURE_AXI_ACELITE_BIT_MASK                            	(IMG_UINT64_C(0x0000000000000001))

#define	RGX_FEATURE_CLUSTER_GROUPING_POS                            	(1U)
#define	RGX_FEATURE_CLUSTER_GROUPING_BIT_MASK                       	(IMG_UINT64_C(0x0000000000000002))

#define	RGX_FEATURE_COMPUTE_POS                                     	(2U)
#define	RGX_FEATURE_COMPUTE_BIT_MASK                                	(IMG_UINT64_C(0x0000000000000004))

#define	RGX_FEATURE_COMPUTE_MORTON_CAPABLE_POS                      	(3U)
#define	RGX_FEATURE_COMPUTE_MORTON_CAPABLE_BIT_MASK                 	(IMG_UINT64_C(0x0000000000000008))

#define	RGX_FEATURE_COMPUTE_OVERLAP_POS                             	(4U)
#define	RGX_FEATURE_COMPUTE_OVERLAP_BIT_MASK                        	(IMG_UINT64_C(0x0000000000000010))

#define	RGX_FEATURE_COMPUTE_OVERLAP_WITH_BARRIERS_POS               	(5U)
#define	RGX_FEATURE_COMPUTE_OVERLAP_WITH_BARRIERS_BIT_MASK          	(IMG_UINT64_C(0x0000000000000020))

#define	RGX_FEATURE_DYNAMIC_DUST_POWER_POS                          	(6U)
#define	RGX_FEATURE_DYNAMIC_DUST_POWER_BIT_MASK                     	(IMG_UINT64_C(0x0000000000000040))

#define	RGX_FEATURE_FASTRENDER_DM_POS                               	(7U)
#define	RGX_FEATURE_FASTRENDER_DM_BIT_MASK                          	(IMG_UINT64_C(0x0000000000000080))

#define	RGX_FEATURE_GPU_CPU_COHERENCY_POS                           	(8U)
#define	RGX_FEATURE_GPU_CPU_COHERENCY_BIT_MASK                      	(IMG_UINT64_C(0x0000000000000100))

#define	RGX_FEATURE_GPU_VIRTUALISATION_POS                          	(9U)
#define	RGX_FEATURE_GPU_VIRTUALISATION_BIT_MASK                     	(IMG_UINT64_C(0x0000000000000200))

#define	RGX_FEATURE_GS_RTA_SUPPORT_POS                              	(10U)
#define	RGX_FEATURE_GS_RTA_SUPPORT_BIT_MASK                         	(IMG_UINT64_C(0x0000000000000400))

#define	RGX_FEATURE_META_DMA_POS                                    	(11U)
#define	RGX_FEATURE_META_DMA_BIT_MASK                               	(IMG_UINT64_C(0x0000000000000800))

#define	RGX_FEATURE_MIPS_POS                                        	(12U)
#define	RGX_FEATURE_MIPS_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000001000))

#define	RGX_FEATURE_PBE2_IN_XE_POS                                  	(13U)
#define	RGX_FEATURE_PBE2_IN_XE_BIT_MASK                             	(IMG_UINT64_C(0x0000000000002000))

#define	RGX_FEATURE_PBVNC_COREID_REG_POS                            	(14U)
#define	RGX_FEATURE_PBVNC_COREID_REG_BIT_MASK                       	(IMG_UINT64_C(0x0000000000004000))

#define	RGX_FEATURE_PDS_PER_DUST_POS                                	(15U)
#define	RGX_FEATURE_PDS_PER_DUST_BIT_MASK                           	(IMG_UINT64_C(0x0000000000008000))

#define	RGX_FEATURE_PDS_TEMPSIZE8_POS                               	(16U)
#define	RGX_FEATURE_PDS_TEMPSIZE8_BIT_MASK                          	(IMG_UINT64_C(0x0000000000010000))

#define	RGX_FEATURE_PERFBUS_POS                                     	(17U)
#define	RGX_FEATURE_PERFBUS_BIT_MASK                                	(IMG_UINT64_C(0x0000000000020000))

#define	RGX_FEATURE_RAY_TRACING_POS                                 	(18U)
#define	RGX_FEATURE_RAY_TRACING_BIT_MASK                            	(IMG_UINT64_C(0x0000000000040000))

#define	RGX_FEATURE_ROGUEXE_POS                                     	(19U)
#define	RGX_FEATURE_ROGUEXE_BIT_MASK                                	(IMG_UINT64_C(0x0000000000080000))

#define	RGX_FEATURE_S7_CACHE_HIERARCHY_POS                          	(20U)
#define	RGX_FEATURE_S7_CACHE_HIERARCHY_BIT_MASK                     	(IMG_UINT64_C(0x0000000000100000))

#define	RGX_FEATURE_S7_TOP_INFRASTRUCTURE_POS                       	(21U)
#define	RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK                  	(IMG_UINT64_C(0x0000000000200000))

#define	RGX_FEATURE_SCALABLE_VDM_GPP_POS                            	(22U)
#define	RGX_FEATURE_SCALABLE_VDM_GPP_BIT_MASK                       	(IMG_UINT64_C(0x0000000000400000))

#define	RGX_FEATURE_SIGNAL_SNOOPING_POS                             	(23U)
#define	RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK                        	(IMG_UINT64_C(0x0000000000800000))

#define	RGX_FEATURE_SINGLE_BIF_POS                                  	(24U)
#define	RGX_FEATURE_SINGLE_BIF_BIT_MASK                             	(IMG_UINT64_C(0x0000000001000000))

#define	RGX_FEATURE_SLCSIZE8_POS                                    	(25U)
#define	RGX_FEATURE_SLCSIZE8_BIT_MASK                               	(IMG_UINT64_C(0x0000000002000000))

#define	RGX_FEATURE_SLC_HYBRID_CACHELINE_64_128_POS                 	(26U)
#define	RGX_FEATURE_SLC_HYBRID_CACHELINE_64_128_BIT_MASK            	(IMG_UINT64_C(0x0000000004000000))

#define	RGX_FEATURE_SLC_VIVT_POS                                    	(27U)
#define	RGX_FEATURE_SLC_VIVT_BIT_MASK                               	(IMG_UINT64_C(0x0000000008000000))

#define	RGX_FEATURE_SYS_BUS_SECURE_RESET_POS                        	(28U)
#define	RGX_FEATURE_SYS_BUS_SECURE_RESET_BIT_MASK                   	(IMG_UINT64_C(0x0000000010000000))

#define	RGX_FEATURE_TESSELLATION_POS                                	(29U)
#define	RGX_FEATURE_TESSELLATION_BIT_MASK                           	(IMG_UINT64_C(0x0000000020000000))

#define	RGX_FEATURE_TLA_POS                                         	(30U)
#define	RGX_FEATURE_TLA_BIT_MASK                                    	(IMG_UINT64_C(0x0000000040000000))

#define	RGX_FEATURE_TPU_CEM_DATAMASTER_GLOBAL_REGISTERS_POS         	(31U)
#define	RGX_FEATURE_TPU_CEM_DATAMASTER_GLOBAL_REGISTERS_BIT_MASK    	(IMG_UINT64_C(0x0000000080000000))

#define	RGX_FEATURE_TPU_DM_GLOBAL_REGISTERS_POS                     	(32U)
#define	RGX_FEATURE_TPU_DM_GLOBAL_REGISTERS_BIT_MASK                	(IMG_UINT64_C(0x0000000100000000))

#define	RGX_FEATURE_TPU_FILTERING_MODE_CONTROL_POS                  	(33U)
#define	RGX_FEATURE_TPU_FILTERING_MODE_CONTROL_BIT_MASK             	(IMG_UINT64_C(0x0000000200000000))

#define	RGX_FEATURE_VDM_DRAWINDIRECT_POS                            	(34U)
#define	RGX_FEATURE_VDM_DRAWINDIRECT_BIT_MASK                       	(IMG_UINT64_C(0x0000000400000000))

#define	RGX_FEATURE_VDM_OBJECT_LEVEL_LLS_POS                        	(35U)
#define	RGX_FEATURE_VDM_OBJECT_LEVEL_LLS_BIT_MASK                   	(IMG_UINT64_C(0x0000000800000000))

#define	RGX_FEATURE_XT_TOP_INFRASTRUCTURE_POS                       	(36U)
#define	RGX_FEATURE_XT_TOP_INFRASTRUCTURE_BIT_MASK                  	(IMG_UINT64_C(0x0000001000000000))

#define	RGX_FEATURE_CDM_CONTROL_STREAM_FORMAT_POS                   	(0U)
#define	RGX_FEATURE_CDM_CONTROL_STREAM_FORMAT_BIT_MASK              	(IMG_UINT64_C(0x0000000000000003))

#define	RGX_FEATURE_FBCDC_ARCHITECTURE_POS                          	(2U)
#define	RGX_FEATURE_FBCDC_ARCHITECTURE_BIT_MASK                     	(IMG_UINT64_C(0x000000000000000C))

#define	RGX_FEATURE_META_POS                                        	(4U)
#define	RGX_FEATURE_META_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000030))

#define	RGX_FEATURE_META_COREMEM_BANKS_POS                          	(6U)
#define	RGX_FEATURE_META_COREMEM_BANKS_BIT_MASK                     	(IMG_UINT64_C(0x00000000000001C0))

#define	RGX_FEATURE_META_COREMEM_SIZE_POS                           	(9U)
#define	RGX_FEATURE_META_COREMEM_SIZE_BIT_MASK                      	(IMG_UINT64_C(0x0000000000000E00))

#define	RGX_FEATURE_META_DMA_CHANNEL_COUNT_POS                      	(12U)
#define	RGX_FEATURE_META_DMA_CHANNEL_COUNT_BIT_MASK                 	(IMG_UINT64_C(0x0000000000003000))

#define	RGX_FEATURE_NUM_CLUSTERS_POS                                	(14U)
#define	RGX_FEATURE_NUM_CLUSTERS_BIT_MASK                           	(IMG_UINT64_C(0x000000000003C000))

#define	RGX_FEATURE_NUM_ISP_IPP_PIPES_POS                           	(18U)
#define	RGX_FEATURE_NUM_ISP_IPP_PIPES_BIT_MASK                      	(IMG_UINT64_C(0x00000000003C0000))

#define	RGX_FEATURE_PHYS_BUS_WIDTH_POS                              	(22U)
#define	RGX_FEATURE_PHYS_BUS_WIDTH_BIT_MASK                         	(IMG_UINT64_C(0x0000000000C00000))

#define	RGX_FEATURE_SCALABLE_TE_ARCH_POS                            	(24U)
#define	RGX_FEATURE_SCALABLE_TE_ARCH_BIT_MASK                       	(IMG_UINT64_C(0x0000000003000000))

#define	RGX_FEATURE_SCALABLE_VCE_POS                                	(26U)
#define	RGX_FEATURE_SCALABLE_VCE_BIT_MASK                           	(IMG_UINT64_C(0x000000000C000000))

#define	RGX_FEATURE_SLC_BANKS_POS                                   	(28U)
#define	RGX_FEATURE_SLC_BANKS_BIT_MASK                              	(IMG_UINT64_C(0x0000000030000000))

#define	RGX_FEATURE_SLC_CACHE_LINE_SIZE_BITS_POS                    	(30U)
#define	RGX_FEATURE_SLC_CACHE_LINE_SIZE_BITS_BIT_MASK               	(IMG_UINT64_C(0x0000000040000000))

#define	RGX_FEATURE_SLC_SIZE_IN_BYTES_POS                           	(31U)
#define	RGX_FEATURE_SLC_SIZE_IN_BYTES_BIT_MASK                      	(IMG_UINT64_C(0x0000000380000000))

#define	RGX_FEATURE_SLC_SIZE_IN_KILOBYTES_POS                       	(31U)
#define	RGX_FEATURE_SLC_SIZE_IN_KILOBYTES_BIT_MASK                  	(IMG_UINT64_C(0x0000000380000000))

#define	RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS_POS                  	(36U)
#define	RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS_BIT_MASK             	(IMG_UINT64_C(0x0000001000000000))

#define	HW_ERN_36400_POS                                            	(0U)
#define	HW_ERN_36400_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000000001))

#define	FIX_HW_BRN_37200_POS                                        	(1U)
#define	FIX_HW_BRN_37200_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000002))

#define	FIX_HW_BRN_37918_POS                                        	(2U)
#define	FIX_HW_BRN_37918_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000004))

#define	FIX_HW_BRN_38344_POS                                        	(3U)
#define	FIX_HW_BRN_38344_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000008))

#define	HW_ERN_41805_POS                                            	(4U)
#define	HW_ERN_41805_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000000010))

#define	HW_ERN_42290_POS                                            	(5U)
#define	HW_ERN_42290_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000000020))

#define	FIX_HW_BRN_42321_POS                                        	(6U)
#define	FIX_HW_BRN_42321_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000040))

#define	FIX_HW_BRN_42480_POS                                        	(7U)
#define	FIX_HW_BRN_42480_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000080))

#define	HW_ERN_42606_POS                                            	(8U)
#define	HW_ERN_42606_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000000100))

#define	FIX_HW_BRN_43276_POS                                        	(9U)
#define	FIX_HW_BRN_43276_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000200))

#define	FIX_HW_BRN_44455_POS                                        	(10U)
#define	FIX_HW_BRN_44455_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000400))

#define	FIX_HW_BRN_44871_POS                                        	(11U)
#define	FIX_HW_BRN_44871_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000000800))

#define	HW_ERN_44885_POS                                            	(12U)
#define	HW_ERN_44885_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000001000))

#define	HW_ERN_45914_POS                                            	(13U)
#define	HW_ERN_45914_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000002000))

#define	HW_ERN_46066_POS                                            	(14U)
#define	HW_ERN_46066_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000004000))

#define	HW_ERN_47025_POS                                            	(15U)
#define	HW_ERN_47025_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000008000))

#define	HW_ERN_49144_POS                                            	(16U)
#define	HW_ERN_49144_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000010000))

#define	HW_ERN_50539_POS                                            	(17U)
#define	HW_ERN_50539_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000020000))

#define	FIX_HW_BRN_50767_POS                                        	(18U)
#define	FIX_HW_BRN_50767_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000040000))

#define	FIX_HW_BRN_51281_POS                                        	(19U)
#define	FIX_HW_BRN_51281_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000080000))

#define	HW_ERN_51468_POS                                            	(20U)
#define	HW_ERN_51468_BIT_MASK                                       	(IMG_UINT64_C(0x0000000000100000))

#define	FIX_HW_BRN_52402_POS                                        	(21U)
#define	FIX_HW_BRN_52402_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000200000))

#define	FIX_HW_BRN_52563_POS                                        	(22U)
#define	FIX_HW_BRN_52563_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000400000))

#define	FIX_HW_BRN_54141_POS                                        	(23U)
#define	FIX_HW_BRN_54141_BIT_MASK                                   	(IMG_UINT64_C(0x0000000000800000))

#define	FIX_HW_BRN_54441_POS                                        	(24U)
#define	FIX_HW_BRN_54441_BIT_MASK                                   	(IMG_UINT64_C(0x0000000001000000))

#define	FIX_HW_BRN_55091_POS                                        	(25U)
#define	FIX_HW_BRN_55091_BIT_MASK                                   	(IMG_UINT64_C(0x0000000002000000))

#define	FIX_HW_BRN_57193_POS                                        	(26U)
#define	FIX_HW_BRN_57193_BIT_MASK                                   	(IMG_UINT64_C(0x0000000004000000))

#define	HW_ERN_57596_POS                                            	(27U)
#define	HW_ERN_57596_BIT_MASK                                       	(IMG_UINT64_C(0x0000000008000000))

#define	FIX_HW_BRN_60084_POS                                        	(28U)
#define	FIX_HW_BRN_60084_BIT_MASK                                   	(IMG_UINT64_C(0x0000000010000000))

#define	HW_ERN_61389_POS                                            	(29U)
#define	HW_ERN_61389_BIT_MASK                                       	(IMG_UINT64_C(0x0000000020000000))

#define	FIX_HW_BRN_61450_POS                                        	(30U)
#define	FIX_HW_BRN_61450_BIT_MASK                                   	(IMG_UINT64_C(0x0000000040000000))

#define	FIX_HW_BRN_62204_POS                                        	(31U)
#define	FIX_HW_BRN_62204_BIT_MASK                                   	(IMG_UINT64_C(0x0000000080000000))

#define	FIX_HW_BRN_63027_POS                                        	(32U)
#define	FIX_HW_BRN_63027_BIT_MASK                                   	(IMG_UINT64_C(0x0000000100000000))

#define	FIX_HW_BRN_63142_POS                                        	(33U)
#define	FIX_HW_BRN_63142_BIT_MASK                                   	(IMG_UINT64_C(0x0000000200000000))



#endif /*_RGX_BVNC_DEFS_KM_H_ */



