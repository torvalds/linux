/*
 * \file       trc_cmp_cfg_etmv3.cpp
 * \brief      OpenCSD : 
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

#include "opencsd/etmv3/trc_cmp_cfg_etmv3.h"

EtmV3Config::EtmV3Config()
{
    // defaults set ETMv3.4, V7A, instruction only.
    m_cfg.arch_ver = ARCH_V7;
    m_cfg.core_prof = profile_CortexA;
    m_cfg.reg_ccer = 0;
    m_cfg.reg_idr = 0x4100F240; // default trace IDR value
    m_cfg.reg_ctrl = 0;
}

EtmV3Config::EtmV3Config(const ocsd_etmv3_cfg *cfg_regs)
{
    m_cfg = *cfg_regs;
}

EtmV3Config::EtmTraceMode const EtmV3Config::GetTraceMode() const
{
    int mode = 0 + ( isDataValTrace() ? 1 : 0 ) + (isDataAddrTrace() ? 2 : 0) + (isInstrTrace() ? 0 : 3);
    return (EtmTraceMode)mode;
}

const int EtmV3Config::CtxtIDBytes() const
{
    int ctxtIdsizes[] = { 0, 1, 2, 4 };
    return ctxtIdsizes[(m_cfg.reg_ctrl >> 14) & 0x3];
}

/* End of File trc_cmp_cfg_etmv3.cpp */
