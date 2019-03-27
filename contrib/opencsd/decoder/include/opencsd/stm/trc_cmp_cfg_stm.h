/*
 * \file       trc_cmp_cfg_stm.h
 * \brief      OpenCSD : STM compnent configuration.
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

#ifndef ARM_TRC_CMP_CFG_STM_H_INCLUDED
#define ARM_TRC_CMP_CFG_STM_H_INCLUDED

#include "trc_pkt_types_stm.h"
#include "common/trc_cs_config.h"

/** @addtogroup ocsd_protocol_cfg
@{*/

/** @name STM configuration
@{*/

/*!
 * @class STMConfig   
 * @brief STM hardware configuration data.
 * 
 *  Represents the programmed and hardware configured state of an STM device.
 *  Creates default values for most RO register values to effect a default STM
 *  with values of 256 masters, 65536 channels, HW event trace not present / disabled.
 *
 *  If this default is sufficient a single call to setTraceID() will be all that is 
 *  required to decode the STM protocol.
 *
 *  Can also be initialised with a fully populated ocsd_stm_cfg structure.
 */
class STMConfig : public CSConfig // public ocsd_stm_cfg
{
public:
    STMConfig();    //!< Constructor - creates a default configuration
    STMConfig(const ocsd_stm_cfg *cfg_regs); 
    ~STMConfig() {};    

// operations to convert to and from C-API structure

    STMConfig & operator=(const ocsd_stm_cfg *p_cfg);  //!< set from full configuration structure.
    //! cast operator returning struct const reference
    operator const ocsd_stm_cfg &() const { return m_cfg; };
    //! cast operator returning struct const pointer
    operator const ocsd_stm_cfg *() const { return &m_cfg; };

// access functions 
    void setTraceID(const uint8_t traceID);     //!< Set the CoreSight trace ID.
    void setHWTraceFeat(const hw_event_feat_t hw_feat); //!< set usage of STM HW event trace.
    
    virtual const uint8_t getTraceID() const;   //!< Get the CoreSight trace ID.
    const uint8_t getMaxMasterIdx() const;      //!< Get the maximum master index
    const uint16_t getMaxChannelIdx() const;    //!< Get the maximum channel index.
    const uint16_t getHWTraceMasterIdx() const; //!< Get the master used for HW event trace.
    bool getHWTraceEn() const; //!< return true if HW trace is present and enabled.

private:
    bool m_bHWTraceEn;  
    ocsd_stm_cfg m_cfg;
};

inline STMConfig::STMConfig()
{
    m_cfg.reg_tcsr = 0;
    m_cfg.reg_devid = 0xFF;   // default to 256 masters.
    m_cfg.reg_feat3r = 0x10000; // default to 65536 channels.
    m_cfg.reg_feat1r = 0x0;
    m_cfg.reg_hwev_mast = 0;    // default hwtrace master = 0;
    m_cfg.hw_event = HwEvent_Unknown_Disabled; // default to not present / disabled.
    m_bHWTraceEn = false;
}
  
inline STMConfig::STMConfig(const ocsd_stm_cfg *cfg_regs)
{
    m_cfg = *cfg_regs;
    setHWTraceFeat(m_cfg.hw_event);
}

inline STMConfig & STMConfig::operator=(const ocsd_stm_cfg *p_cfg)
{
    m_cfg = *p_cfg;
    setHWTraceFeat(p_cfg->hw_event);
    return *this;
}

inline void STMConfig::setTraceID(const uint8_t traceID)
{
    uint32_t IDmask = 0x007F0000;
    m_cfg.reg_tcsr &= ~IDmask;
    m_cfg.reg_tcsr |= (((uint32_t)traceID) << 16) & IDmask;
}

inline void STMConfig::setHWTraceFeat(const hw_event_feat_t hw_feat)
{
    m_cfg.hw_event = hw_feat;
    m_bHWTraceEn = (m_cfg.hw_event == HwEvent_Enabled);
    if(m_cfg.hw_event == HwEvent_UseRegisters)
        m_bHWTraceEn = (((m_cfg.reg_feat1r & 0xC0000) == 0x80000) && ((m_cfg.reg_tcsr & 0x8) == 0x8));
}

inline const uint8_t STMConfig::getTraceID() const
{
    return (uint8_t)((m_cfg.reg_tcsr >> 16) & 0x7F);
}

inline const uint8_t STMConfig::getMaxMasterIdx() const
{
    return (uint8_t)(m_cfg.reg_devid & 0xFF);
}

inline const uint16_t STMConfig::getMaxChannelIdx() const
{
    return (uint16_t)(m_cfg.reg_feat3r - 1);
}

inline const uint16_t STMConfig::getHWTraceMasterIdx() const
{
    return (uint16_t)(m_cfg.reg_hwev_mast & 0xFFFF);
}

inline bool STMConfig::getHWTraceEn() const
{       
    return m_bHWTraceEn;
}


/** @}*/

/** @}*/

#endif // ARM_TRC_CMP_CFG_STM_H_INCLUDED

/* End of File trc_cmp_cfg_stm.h */
