/*
* \file       ocsd_c_api_cust_fact.h
* \brief      OpenCSD : Custom decoder factory API functions
*
* \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
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
#ifndef ARM_OCSD_C_API_CUST_FACT_H_INCLUDED
#define ARM_OCSD_C_API_CUST_FACT_H_INCLUDED

#include "ocsd_c_api_types.h"
#include "ocsd_c_api_custom.h"

/* Declarations for the functions implemented in the custom decoder factory. */

/** Required function to create a decoder instance - fills in the decoder struct supplied. */
ocsd_err_t CreateCustomDecoder(const int create_flags, const void *decoder_cfg, ocsd_extern_dcd_inst_t *p_decoder_inst);

/** Required Function to destroy a decoder instance - indicated by decoder handle */
ocsd_err_t DestroyCustomDecoder(const void *decoder_handle);

/** Required Function to extract the CoreSight Trace ID from the configuration structure */
ocsd_err_t GetCSIDFromConfig(const void *decoder_cfg, unsigned char *p_csid);

/** Optional Function to convert a protocol specific trace packet to human readable string */
ocsd_err_t PacketToString(const void *trc_pkt, char *buffer, const int buflen);

#endif /* ARM_OCSD_C_API_CUST_FACT_H_INCLUDED */
