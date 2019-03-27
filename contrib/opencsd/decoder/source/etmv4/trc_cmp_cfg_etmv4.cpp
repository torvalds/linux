/*
 * \file       trc_cmp_cfg_etmv4.cpp
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

#include "opencsd/etmv4/trc_cmp_cfg_etmv4.h"

EtmV4Config::EtmV4Config()   
{
    m_cfg.reg_idr0 = 0x28000EA1;
    m_cfg.reg_idr1 = 0x4100F403;   
    m_cfg.reg_idr2 = 0x00000488;       
    m_cfg.reg_idr8 = 0;    
    m_cfg.reg_idr9 = 0;   
    m_cfg.reg_idr10 = 0;   
    m_cfg.reg_idr11 = 0;   
    m_cfg.reg_idr12 = 0;   
    m_cfg.reg_idr13 = 0;   
    m_cfg.reg_configr = 0xC1; 
    m_cfg.reg_traceidr = 0;
    m_cfg.arch_ver = ARCH_V7;
    m_cfg.core_prof = profile_CortexA;

    PrivateInit();
}

EtmV4Config::EtmV4Config(const ocsd_etmv4_cfg *cfg_regs)
{
    m_cfg = *cfg_regs;
    PrivateInit();
}

EtmV4Config & EtmV4Config::operator=(const ocsd_etmv4_cfg *p_cfg)
{
    m_cfg = *p_cfg;
    PrivateInit();
    return *this;
}

void EtmV4Config::PrivateInit()
{
    m_QSuppCalc = false;
    m_QSuppFilter = false;
    m_QSuppType = Q_NONE;
    m_VMIDSzCalc = false;
    m_VMIDSize = 0;
    m_condTraceCalc = false;
    m_CondTrace = COND_TR_DIS;
}

void EtmV4Config::CalcQSupp()
{
    QSuppType qtypes[] = {
        Q_NONE,
        Q_ICOUNT_ONLY,
        Q_NO_ICOUNT_ONLY,
        Q_FULL
    };
    uint8_t Qsupp = (m_cfg.reg_idr0 >> 15) & 0x3;
    m_QSuppType = qtypes[Qsupp];
    m_QSuppFilter = (bool)((m_cfg.reg_idr0 & 0x4000) == 0x4000) && (m_QSuppType != Q_NONE);
    m_QSuppCalc = true;
}

void EtmV4Config::CalcVMIDSize()
{
    uint32_t vmidszF = (m_cfg.reg_idr2 >> 10) & 0x1F;
    if(vmidszF == 1)
        m_VMIDSize = 8;
    else if(MinVersion() > 0)
    {
        if(vmidszF == 2)
            m_VMIDSize = 16;
        else if(vmidszF == 4)
            m_VMIDSize = 32;
    }
    m_VMIDSzCalc = true;
}

/* End of File trc_cmp_cfg_etmv4.cpp */
