/*!
 * \file       opencsd.h
 * \brief      OpenCSD: Open CoreSight Trace Decoder -Master include file for C++ library
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#ifndef ARM_OPENCSD_H_INCLUDED
#define ARM_OPENCSD_H_INCLUDED

/** C interface types */
#include "opencsd/ocsd_if_types.h"
#include "opencsd/trc_pkt_types.h"
#include "opencsd/trc_gen_elem_types.h"

/* C++ abstract interfaces */
#include "interfaces/trc_data_raw_in_i.h"
#include "interfaces/trc_data_rawframe_in_i.h"
#include "interfaces/trc_error_log_i.h"
#include "interfaces/trc_gen_elem_in_i.h"
#include "interfaces/trc_instr_decode_i.h"
#include "interfaces/trc_pkt_in_i.h"
#include "interfaces/trc_pkt_raw_in_i.h"
#include "interfaces/trc_tgt_mem_access_i.h"

/* protocol base classes and generic elements */
#include "common/ocsd_version.h"
#include "common/ocsd_error.h"
#include "common/trc_gen_elem.h"
#include "common/trc_core_arch_map.h"

/** Implemented Protocol decoders */
#include "common/trc_frame_deformatter.h"

#include "opencsd/etmv3/etmv3_decoder.h"
#include "opencsd/etmv4/etmv4_decoder.h"
#include "opencsd/ptm/ptm_decoder.h"
#include "opencsd/stm/stm_decoder.h"

/** C++ library object types */
#include "common/ocsd_error_logger.h"
#include "common/ocsd_msg_logger.h"
#include "i_dec/trc_i_decode.h"
#include "mem_acc/trc_mem_acc.h"

/* printers for builtin packet elements */
#include "pkt_printers/trc_pkt_printers.h"
#include "pkt_printers/trc_print_fact.h"

/** The decode tree and decoder register*/
#include "common/ocsd_lib_dcd_register.h"
#include "common/ocsd_dcd_tree.h"


#endif // ARM_OPENCSD_H_INCLUDED

/* End of File opencsd.h */
